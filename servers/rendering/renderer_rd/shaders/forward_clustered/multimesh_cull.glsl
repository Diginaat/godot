#[compute]

#version 450

#VERSION_DEFINES

#ifdef MODE_SCAN
#define SCAN_WIDTH 256
#else
#define SCAN_WIDTH 64
#endif
layout(local_size_x = SCAN_WIDTH, local_size_y = 1, local_size_z = 1) in;

// Up to four LOD buckets per surface (levels 0-3 of Godot's index LODs).
// Each bucket owns its own ID range and indirect command record.
#define MAX_LOD_LEVELS 4u

struct CullData {
	vec4 planes[6]; // Outward planes in MultiMesh-local space: dot(n, p) > d is outside.
	vec4 center; // w: distance thinning begin (local units), see is_visible().
	vec4 half_extents; // w: distance thinning end; 0 disables thinning.
	uvec4 params; // Input count, transform stride (vec4s), current transform offset, output ID offset.
	vec4 lod_camera; // Camera position in MultiMesh-local space.
	// Prescaled LOD edge lengths: an instance uses level k + 1 while
	// lod_edges[k] * instance scale <= local distance (Godot's CPU LOD rule).
	vec4 lod_edges;
	uvec4 lod_params; // Level count (1-4), IDs reserved per level (64-aligned), first command record, first statistics word.
};

// Set 0 is shared by every draw and bound once per mode; set 1 holds the
// MultiMesh's own transform buffer and exists only in the cull mode.
layout(set = 0, binding = 0, std430) readonly buffer CullInputs {
	CullData data[];
}
inputs;
layout(set = 0, binding = 1, std430) writeonly buffer VisibleIDs {
	uint data[];
}
visible_ids;
layout(set = 0, binding = 2, std430) buffer Commands {
	uint data[];
}
commands;
layout(set = 0, binding = 3, std430) buffer Ranks {
	uint data[];
}
ranks;
layout(set = 0, binding = 4, std430) buffer Blocks {
	uint data[];
}
blocks;
#ifdef GEOMETRY_STATISTICS
// Viewport counters (word layout in geometry_statistics.cpp), read back
// asynchronously only while requested. lod_params.w selects the main-view or
// shadow word block.
layout(set = 0, binding = 5, std430) buffer Statistics {
	uint data[];
}
statistics;
#endif
#if !defined(MODE_SCAN) && !defined(MODE_SCATTER) && !defined(MODE_SPLIT) && !defined(MODE_PHASE2)
layout(set = 1, binding = 0, std430) readonly buffer Transforms {
	vec4 data[];
}
transforms;
#endif
// Rank words: bit 31 selects the instance in the current ranking, bits 24-29
// hold its LOD level, bits 0-23 its rank in the workgroup. MODE_SPLIT also
// sets bit 30 on every frustum-visible instance, as it no longer selects all.
#if defined(MODE_OCCLUDE) || defined(MODE_SPLIT) || defined(MODE_PHASE2)
// HZB occlusion of the frustum-visible instances. Runs after the depth
// pre-pass; reads the frustum pass's ranks (visibility and LOD level) and
// ranks again, so the scan and scatter modes then compact the unoccluded
// instances into a second ID and command buffer for the opaque colour passes.
// With two-pass occlusion, MODE_SPLIT first selects the instances that were
// visible last frame for the depth pre-pass (phase 1), MODE_OCCLUDE records
// this frame's visibility, and MODE_PHASE2 selects the visible instances that
// phase 1 did not draw, for a second depth pass.
struct OcclusionData {
	mat4 local_to_clip; // MultiMesh-local space to jittered clip space.
	mat4 abs_to_clip; // abs(P) * abs(V) * abs(M), bounds the rounding of local_to_clip.
	uvec4 params; // Actual depth width/height, previous and current history offset (NO_HISTORY: none).
};
layout(set = 0, binding = 6, std430) readonly buffer OcclusionInputs {
	OcclusionData data[];
}
occlusion;
// One word per instance of each MultiMesh: visible after occlusion. The
// previous frame's buffer is read, the current one written.
layout(set = 0, binding = 8, std430) readonly buffer PreviousHistory {
	uint data[];
}
previous_history;
layout(set = 0, binding = 9, std430) writeonly buffer History {
	uint data[];
}
history;

const uint NO_HISTORY = 0xffffffffu;
const uint STAT_MULTIMESH_DEPTH_INSTANCES = 31u;
#endif
#ifdef MODE_OCCLUDE
layout(set = 0, binding = 7) uniform sampler2D depth_pyramid;

#include "mesh_hiz_inc.glsl"

const uint STAT_HZB_INSTANCES_OCCLUDED = 28u;
#endif
// Cull and occlusion dispatch once per draw (each binds its MultiMesh's
// transforms; draw_index is the draw). The other modes run one dispatch for
// all draws, where draw_index is the number of workgroups: the scan one
// workgroup per draw, scatter, split and phase 2 through group_draws.
layout(push_constant, std430) uniform Params {
	uint draw_index;
}
params;

#if defined(MODE_SCATTER) || defined(MODE_SPLIT) || defined(MODE_PHASE2)
#define BATCHED_GROUPS
// Per workgroup of the batched dispatch: its draw and its workgroup in that draw.
layout(set = 0, binding = 10, std430) readonly buffer GroupDraws {
	uvec2 data[];
}
group_draws;
#endif

uint draw_index;
uint group_index;

#if defined(MODE_OCCLUDE) || defined(MODE_SPLIT) || defined(MODE_PHASE2)
// Phase 1 draws an instance when it was visible last frame, or when the
// MultiMesh has no history (new to the view, or resized).
bool drawn_in_phase1(uint instance_id) {
	uint offset = occlusion.data[draw_index].params.z;
	return offset == NO_HISTORY || previous_history.data[offset + instance_id] != 0u;
}
#endif

shared uint scan_data[SCAN_WIDTH];
shared uint carry;

uint inclusive_scan(uint value) {
	uint lane = gl_LocalInvocationIndex;
	scan_data[lane] = value;
	barrier();
	for (uint step = 1u; step < SCAN_WIDTH; step <<= 1u) {
		uint previous = lane >= step ? scan_data[lane - step] : 0u;
		barrier();
		scan_data[lane] += previous;
		barrier();
	}
	return scan_data[lane];
}

#if !defined(MODE_SCAN) && !defined(MODE_SCATTER) && !defined(MODE_OCCLUDE) && !defined(MODE_SPLIT) && !defined(MODE_PHASE2)
// Stable per-instance value in [0, 1): an instance is either always kept or
// always dropped at a given distance, so thinning never flickers.
float thinning_hash(uint x) {
	x ^= x >> 16u;
	x *= 0x7feb352du;
	x ^= x >> 15u;
	x *= 0x846ca68bu;
	x ^= x >> 16u;
	return float(x >> 8u) * (1.0 / 16777216.0);
}

bool is_visible(CullData input_data, uint instance_id, out uint level) {
	level = 0u;
	uint offset = (instance_id + input_data.params.z) * input_data.params.y;
	vec4 r0 = transforms.data[offset];
	vec4 r1 = transforms.data[offset + 1u];
	vec4 r2 = transforms.data[offset + 2u];
	vec4 center = vec4(input_data.center.xyz, 1.0);
	vec3 transformed_center = vec3(dot(r0, center), dot(r1, center), dot(r2, center));
	vec3 extent = input_data.half_extents.xyz;
	vec3 transformed_extent = vec3(dot(abs(r0.xyz), extent), dot(abs(r1.xyz), extent), dot(abs(r2.xyz), extent));
	for (uint i = 0u; i < 6u; i++) {
		vec4 plane = input_data.planes[i];
		float distance_to_plane = dot(plane.xyz, transformed_center) - plane.w;
		float radius = dot(abs(plane.xyz), transformed_extent);
		// Retain boundary cases and uncertain/non-finite transforms conservatively.
		float margin = 0.0001 * (1.0 + dot(abs(plane.xyz), abs(transformed_center)) + abs(plane.w) + radius);
		if (distance_to_plane > radius + margin) {
			return false;
		}
	}
	float thinning_end = input_data.half_extents.w;
	if (thinning_end > 0.0) {
		// Distance thinning: the kept fraction falls linearly from 1 at the
		// begin distance to 0 at the end distance (instance-centre distance).
		float distance = length(transformed_center - input_data.lod_camera.xyz);
		float begin = input_data.center.w;
		float kept = clamp((thinning_end - distance) / max(thinning_end - begin, 1e-6), 0.0, 1.0);
		if (thinning_hash(instance_id) >= kept) {
			return false;
		}
	}
	uint levels = input_data.lod_params.x;
	if (levels > 1u) {
		// Nearest point of the transformed box, as the CPU path measures a whole
		// MultiMesh. The largest basis column bounds the instance's mesh scale.
		vec3 nearest = max(abs(transformed_center - input_data.lod_camera.xyz) - transformed_extent, vec3(0.0));
		float distance = length(nearest);
		float scale = max(length(vec3(r0.x, r1.x, r2.x)), max(length(vec3(r0.y, r1.y, r2.y)), length(vec3(r0.z, r1.z, r2.z))));
		for (uint k = 0u; k + 1u < levels; k++) {
			// Non-finite values compare false and keep the finer level.
			if (!(input_data.lod_edges[k] * scale <= distance)) {
				break;
			}
			level = k + 1u;
		}
	}
	return true;
}
#endif

void main() {
#if defined(MODE_SCAN) || defined(BATCHED_GROUPS)
	// Wide dispatches spill into y (65535 workgroups per dimension).
	uint group = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
	if (group >= params.draw_index) {
		return;
	}
#ifdef MODE_SCAN
	draw_index = group;
	group_index = 0u;
#else
	uvec2 entry = group_draws.data[group];
	draw_index = entry.x;
	group_index = entry.y;
#endif
#else
	draw_index = params.draw_index;
	group_index = gl_WorkGroupID.x;
#endif
	CullData input_data = inputs.data[draw_index];
	// Each draw reserves levels * stride IDs; level l uses [l * stride, (l + 1) * stride)
	// and its block counts start at block_offset + l * level_blocks.
	uint block_offset = input_data.params.w / 64u;
	uint levels = input_data.lod_params.x;
	uint stride = input_data.lod_params.y;
	uint level_blocks = stride / 64u;
#ifdef MODE_SCAN
	uint block_count = (input_data.params.x + 63u) / 64u;
	uint lane = gl_LocalInvocationIndex;
	for (uint level = 0u; level < levels; level++) {
		uint level_offset = block_offset + level * level_blocks;
		if (lane == 0u) {
			carry = 0u;
		}
		barrier();
		for (uint first = 0u; first < block_count; first += SCAN_WIDTH) {
			uint block = first + lane;
			uint value = block < block_count ? blocks.data[level_offset + block] : 0u;
			uint prefix = inclusive_scan(value);
			if (block < block_count) {
				blocks.data[level_offset + block] = carry + prefix - value;
			}
			barrier();
			if (lane == SCAN_WIDTH - 1u) {
				carry += prefix;
			}
			barrier();
		}
		if (lane == 0u) {
			commands.data[(input_data.lod_params.z + level) * 5u + 1u] = carry;
#if defined(GEOMETRY_STATISTICS) && !defined(SCAN_NO_STATISTICS)
			if (level == 0u) {
				atomicAdd(statistics.data[input_data.lod_params.w], input_data.params.x);
			}
			atomicAdd(statistics.data[input_data.lod_params.w + 1u], carry);
			atomicAdd(statistics.data[input_data.lod_params.w + 8u + level], carry);
#endif
		}
		barrier();
	}
#elif defined(MODE_SCATTER)
	uint instance_id = group_index * 64u + gl_LocalInvocationIndex;
	if (instance_id < input_data.params.x) {
		uint rank = ranks.data[input_data.params.w + instance_id];
		if ((rank & 0x80000000u) != 0u) {
			uint level = (rank >> 24u) & 0x3fu;
			uint slot = blocks.data[block_offset + level * level_blocks + instance_id / 64u] + (rank & 0xffffffu);
			visible_ids.data[input_data.params.w + level * stride + slot] = instance_id;
		}
	}
#else
	uint instance_id = group_index * 64u + gl_LocalInvocationIndex;
	uint level = 0u;
	uint keep = 0u; // Rank bits kept on unselected instances (MODE_SPLIT).
#ifdef MODE_OCCLUDE
	bool visible = false;
	if (instance_id < input_data.params.x) {
		uint frustum_rank = ranks.data[input_data.params.w + instance_id];
		// Bit 31 from the frustum pass, or bit 30 after MODE_SPLIT.
		if ((frustum_rank & 0xc0000000u) != 0u) {
			level = (frustum_rank >> 24u) & 0x3fu;
			// The instance box in MultiMesh-local space, as in is_visible().
			uint offset = (instance_id + input_data.params.z) * input_data.params.y;
			vec4 r0 = transforms.data[offset];
			vec4 r1 = transforms.data[offset + 1u];
			vec4 r2 = transforms.data[offset + 2u];
			vec4 center = vec4(input_data.center.xyz, 1.0);
			vec3 transformed_center = vec3(dot(r0, center), dot(r1, center), dot(r2, center));
			vec3 extent = input_data.half_extents.xyz;
			vec3 transformed_extent = vec3(dot(abs(r0.xyz), extent), dot(abs(r1.xyz), extent), dot(abs(r2.xyz), extent));
			OcclusionData occlusion_data = occlusion.data[draw_index];
			// Rounding bound: the CPU matrix bound times the largest local
			// magnitude of the box, with a margin for the instance transform.
			vec3 magnitude = abs(transformed_center) + transformed_extent + 1.0;
			vec4 error = (occlusion_data.abs_to_clip * vec4(magnitude, 1.0)) * (128.0 * 1.1920929e-7);
			visible = !hzb_box_occluded(transformed_center - transformed_extent, transformed_extent * 2.0, occlusion_data.local_to_clip, error, ivec2(occlusion_data.params.xy));
#ifdef GEOMETRY_STATISTICS
			if (!visible) {
				atomicAdd(statistics.data[STAT_HZB_INSTANCES_OCCLUDED], 1u);
			}
#endif
		}
		uint history_offset = occlusion.data[draw_index].params.w;
		if (history_offset != NO_HISTORY) {
			history.data[history_offset + instance_id] = visible ? 1u : 0u;
		}
	}
#elif defined(MODE_SPLIT)
	bool visible = false;
	if (instance_id < input_data.params.x) {
		uint frustum_rank = ranks.data[input_data.params.w + instance_id];
		if ((frustum_rank & 0x80000000u) != 0u) {
			level = (frustum_rank >> 24u) & 0x3fu;
			keep = 0x40000000u;
			visible = drawn_in_phase1(instance_id);
		}
	}
#elif defined(MODE_PHASE2)
	bool visible = false;
	if (instance_id < input_data.params.x) {
		uint rank = ranks.data[input_data.params.w + instance_id];
		if ((rank & 0x80000000u) != 0u) {
			level = (rank >> 24u) & 0x3fu;
			visible = !drawn_in_phase1(instance_id);
		}
	}
#else
	bool visible = instance_id < input_data.params.x && is_visible(input_data, instance_id, level);
#endif
	// One byte per level: 64 lanes never overflow a byte, so a single scan
	// ranks all four buckets at once and keeps source order inside each.
	uint prefix = inclusive_scan(visible ? (1u << (8u * level)) : 0u);
	if (instance_id < input_data.params.x) {
		uint rank = ((prefix >> (8u * level)) & 0xffu) - 1u;
		uint kept = keep != 0u ? (keep | (level << 24u)) : 0u;
		ranks.data[input_data.params.w + instance_id] = visible ? (0x80000000u | kept | (level << 24u) | rank) : kept;
	}
	if (gl_LocalInvocationIndex == 63u) {
		uint selected = 0u;
		for (uint l = 0u; l < levels; l++) {
			uint count = (prefix >> (8u * l)) & 0xffu;
			blocks.data[block_offset + l * level_blocks + group_index] = count;
			selected += count;
		}
#if defined(GEOMETRY_STATISTICS) && (defined(MODE_SPLIT) || defined(MODE_PHASE2))
		if (selected > 0u) {
			atomicAdd(statistics.data[STAT_MULTIMESH_DEPTH_INSTANCES], selected);
		}
#endif
	}
#endif
}
