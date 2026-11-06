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
#if !defined(MODE_SCAN) && !defined(MODE_SCATTER)
layout(set = 1, binding = 0, std430) readonly buffer Transforms {
	vec4 data[];
}
transforms;
#endif
layout(push_constant, std430) uniform Params {
	uint draw_index;
}
params;

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

#if !defined(MODE_SCAN) && !defined(MODE_SCATTER)
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
	CullData input_data = inputs.data[params.draw_index];
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
#ifdef GEOMETRY_STATISTICS
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
	uint instance_id = gl_GlobalInvocationID.x;
	if (instance_id < input_data.params.x) {
		uint rank = ranks.data[input_data.params.w + instance_id];
		if ((rank & 0x80000000u) != 0u) {
			uint level = (rank >> 24u) & 0x7fu;
			uint slot = blocks.data[block_offset + level * level_blocks + instance_id / 64u] + (rank & 0xffffffu);
			visible_ids.data[input_data.params.w + level * stride + slot] = instance_id;
		}
	}
#else
	uint instance_id = gl_GlobalInvocationID.x;
	uint level = 0u;
	bool visible = instance_id < input_data.params.x && is_visible(input_data, instance_id, level);
	// One byte per level: 64 lanes never overflow a byte, so a single scan
	// ranks all four buckets at once and keeps source order inside each.
	uint prefix = inclusive_scan(visible ? (1u << (8u * level)) : 0u);
	if (instance_id < input_data.params.x) {
		uint rank = ((prefix >> (8u * level)) & 0xffu) - 1u;
		ranks.data[input_data.params.w + instance_id] = visible ? (0x80000000u | (level << 24u) | rank) : 0u;
	}
	if (gl_LocalInvocationIndex == 63u) {
		for (uint l = 0u; l < levels; l++) {
			blocks.data[block_offset + l * level_blocks + gl_WorkGroupID.x] = (prefix >> (8u * l)) & 0xffu;
		}
	}
#endif
}
