#[compute]

#version 450

#VERSION_DEFINES

// GPU path for eligible instances (docs/renderer, Phase 2). One thread per
// bucket member (an instance surface): if the CPU scene cull found its
// instance visible (a bit per table slot), the index LOD as the CPU render
// list chooses it, whether the motion pass draws it, and the member's index
// appended to its bucket level's draw. Forward+ builds the instance data
// from the member, its table record and bucket (gpu_instance_data() in
// scene_forward_clustered_inc.glsl, OD10). The commands were cleared on the
// GPU; the first member of a command writes its index count.
//
// MODE_OCCLUDE runs after the depth pre-pass with the camera run's
// parameters: it chooses the same command for each drawn member, tests the
// member's world box against the depth pyramid built from that pre-pass,
// and appends the members it cannot prove hidden to a second set of
// commands and IDs (same layout), which the colour passes draw.
//
// Two-pass (VIEW_FLAG_TWO_PASS, rendering/geometry/gpu_instances/occlusion_two_pass):
// the camera run also appends the drawn members whose instance was visible
// when last tested (history word 0, per table slot) to the phase-1 commands
// and IDs, which the depth pre-pass draws, and records that per member.
// MODE_OCCLUDE then appends the members visible now and not in phase 1 to
// the phase-2 commands and IDs (a second depth pass) and writes the history.
// The phase-1 depth is a subset of the final depth, so the test stays
// conservative whatever the history says.
//
// MODE_SHADOW_BATCH culls all shadow passes of a render in one dispatch: a
// workgroup row per pass, whose LOD inputs and offsets into the commands,
// IDs and visible bits come from ShadowPasses. It writes no instance data
// (the camera run did).

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Record { // GPUInstanceTable::Record, 160 bytes.
	vec4 transform[3];
	vec4 prev_transform[3];
	uint flags;
	uint instance_uniforms_ofs;
	uint layer_mask;
	uint generation;
	vec4 lightmap_uv_scale;
	vec3 aabb_position;
	uint gpu_flags;
	vec3 aabb_size;
	float lod_scale;
};

struct Bucket { // RenderForwardClustered::GPUInstanceBucketData, 80 bytes.
	vec3 compressed_aabb_position;
	uint ids_first; // Visible IDs of level 0; count are reserved per level.
	vec3 compressed_aabb_size;
	uint material_feedback_index;
	vec4 uv_scale;
	uint command; // Indirect command of level 0.
	uint capacity; // Visible IDs reserved per command.
	uint lod_count;
	uint lod_edges_first;
	uint depth_command; // 0xFFFFFFFF: the depth pre-pass draws with command.
	uint pad0;
	uint pad1;
	uint pad2;
};

layout(set = 0, binding = 0, std430) restrict readonly buffer Table {
	Record records[];
}
table;

layout(set = 0, binding = 1, std430) restrict readonly buffer Members {
	uvec4 data[]; // Table slot, bucket, material table slot, unused.
}
members;

layout(set = 0, binding = 2, std430) restrict readonly buffer Buckets {
	Bucket data[];
}
buckets;

layout(set = 0, binding = 3, std430) restrict buffer Commands {
	uint data[]; // Five words per bucket; word 1 is the instance count.
}
commands;

layout(set = 0, binding = 5, std430) restrict writeonly buffer IDs {
	uint data[];
}
ids;

layout(set = 0, binding = 6, std430) restrict readonly buffer LODEdges {
	float data[];
}
lod_edges;

#ifndef MODE_OCCLUDE
layout(set = 0, binding = 7, std430) restrict readonly buffer CommandPrimitives {
	uint data[]; // Primitives per instance of each command.
}
command_primitives;

layout(set = 0, binding = 8, std430) restrict buffer Stats {
	uint objects; // As the CPU lists count them (moved surfaces twice in a view with a motion pass).
	uint primitives;
#ifdef MODE_SHADOW_BATCH
	uint receivers_tested; // Members tested against a receiver mask.
	uint receivers_culled; // Primitives of the members it culled.
#endif
}
stats;

shared uint group_objects;
shared uint group_primitives;
#ifdef MODE_SHADOW_BATCH
shared uint group_receivers_tested;
shared uint group_receivers_culled;
#endif
#endif

#define VIEW_FLAG_USE_GI_BUFFERS 1
#define VIEW_FLAG_USE_SDFGI 2
#define VIEW_FLAG_ORTHOGONAL 4
#define VIEW_FLAG_MOTION 8
#define VIEW_FLAG_SHADOW 16
#define VIEW_FLAG_TWO_PASS 32

#define GPU_FLAG_MOVED 1
#define GPU_FLAG_CAN_SDFGI 2

layout(set = 0, binding = 9, std430) restrict readonly buffer Visible {
	uint bits[]; // Per table slot: the scene cull found the instance visible in this view.
}
visible;

layout(set = 0, binding = 13, std430) restrict readonly buffer CommandIndices {
	uint data[]; // Index count (word 0) of each command, relative to the first.
}
command_indices;

// Appends a member to a command; the first one writes the command's index
// count (the clear zeroed all five words).
uint append_member(uint p_command_offset, uint p_command) {
	uint rank = atomicAdd(commands.data[(p_command_offset + p_command) * 5 + 1], 1);
	if (rank == 0) {
		commands.data[(p_command_offset + p_command) * 5] = command_indices.data[p_command];
	}
	return rank;
}

#ifndef MODE_SHADOW_BATCH
// Two-pass, see the top. Per table slot: 1 if occluded when last tested.
layout(set = 0, binding = 14, std430) restrict buffer History {
	uint data[];
}
history;

// Per member: 1 if the camera run put it in phase 1.
layout(set = 0, binding = 15, std430) restrict buffer Phase1 {
	uint data[];
}
phase1;

// Phase-1 lists in the camera run, phase-2 lists in MODE_OCCLUDE.
layout(set = 0, binding = 16, std430) restrict buffer PhaseCommands {
	uint data[];
}
phase_commands;

layout(set = 0, binding = 17, std430) restrict writeonly buffer PhaseIDs {
	uint data[];
}
phase_ids;

uint append_phase_member(uint p_command) {
	uint rank = atomicAdd(phase_commands.data[p_command * 5 + 1], 1);
	if (rank == 0) {
		phase_commands.data[p_command * 5] = command_indices.data[p_command];
	}
	return rank;
}

void append_phase(uint p_index, Bucket p_bucket, uint p_command) {
	uint rank = append_phase_member(p_bucket.command + p_command);
	phase_ids.data[p_bucket.ids_first + p_command * p_bucket.capacity + rank] = p_index;
	if (p_bucket.depth_command != 0xFFFFFFFFu) {
		append_phase_member(p_bucket.depth_command + p_command);
	}
}
#endif

#ifdef MODE_SHADOW_BATCH
struct ShadowPass { // RenderForwardClustered::GPUInstanceShadowPassData, 48 bytes.
	vec3 lod_camera;
	float lod_threshold;
	float lod_distance_multiplier;
	uint view_flags;
	uint command_offset; // Added to the bucket's command.
	uint ids_offset; // Added to the bucket's first visible ID.
	uint visible_offset; // Words of the visible bits before the pass's.
	uint receiver_grid; // Directional cascade's receiver mask, or 0xFFFFFFFF.
	uint pad1;
	uint pad2;
};

layout(set = 0, binding = 10, std430) restrict readonly buffer ShadowPasses {
	ShadowPass data[];
}
shadow_passes;

layout(set = 0, binding = 11, std430) restrict readonly buffer ReceiverCells {
	uint data[];
}
cells;

layout(set = 0, binding = 12, std430) restrict readonly buffer ReceiverGrids {
	mat4 clip_to_world;
	vec4 data[];
}
grids;

#include "shadow_receivers_inc.glsl"
#endif

#ifdef MODE_OCCLUDE
layout(set = 0, binding = 10, std430) restrict readonly buffer Occlusion {
	mat4 world_to_clip; // Jittered, as the colour passes rasterize.
	mat4 abs_to_clip; // abs(P) * abs(V), bounds the rounding of world_to_clip.
	uvec4 size; // Depth width and height.
}
occlusion;

layout(set = 0, binding = 11) uniform sampler2D depth_pyramid;

layout(set = 0, binding = 12, std430) restrict buffer Statistics {
	uint data[];
}
statistics;

// The word after the MultiMesh one in geometry_statistics.cpp.
const uint STAT_GPU_INSTANCES_OCCLUDED = 29u;
const uint STAT_GPU_INSTANCES_DEPTH_DEFERRED = 33u;

#include "mesh_hiz_inc.glsl"
#endif

layout(push_constant, std430) uniform Params {
	uint member_count;
	uint view_flags;
	float lod_threshold; // Screen mesh LOD threshold; 0 or less: no LODs.
	float lod_distance_multiplier;
	vec3 lod_camera;
	uint pass_first; // MODE_SHADOW_BATCH: the pass of workgroup row 0.
}
params;

// The member's draw command, relative to its bucket's first: the index LOD
// as mesh_surface_get_lod() chooses it for the CPU list and, in a view with
// a motion pass, the moved members' own commands after the others' (as the
// CPU list's motion list).
uint member_command(Record record, Bucket bucket, float p_lod_threshold, float p_lod_distance_multiplier, vec3 p_lod_camera, uint p_view_flags) {
	vec3 aabb_min = record.aabb_position;
	vec3 aabb_max = record.aabb_position + record.aabb_size;
	uint level = 0;
	if (bucket.lod_count > 0 && p_lod_threshold > 0.0) {
		float distance = 1.0;
		if (!bool(p_view_flags & VIEW_FLAG_ORTHOGONAL)) {
			distance = length(max(max(vec3(0.0), aabb_min - p_lod_camera), p_lod_camera - aabb_max));
		}
		float distance_threshold = distance * p_lod_distance_multiplier;
		for (uint i = 0; i < bucket.lod_count; i++) {
			float screen_size = lod_edges.data[bucket.lod_edges_first + i] * record.lod_scale / distance_threshold;
			if (screen_size > p_lod_threshold) {
				break;
			}
			level = i + 1;
		}
	}
	bool moved = bool(record.gpu_flags & GPU_FLAG_MOVED);
	bool motion_pass = bool(p_view_flags & VIEW_FLAG_MOTION);
	return (motion_pass && moved ? bucket.lod_count + 1 : 0) + level;
}

#ifdef MODE_OCCLUDE

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= params.member_count) {
		return;
	}
	uvec4 member = members.data[index];
	if (member.x == 0xFFFFFFFFu || (visible.bits[member.x >> 5] & (1u << (member.x & 31u))) == 0) {
		return; // A free member slot, or not drawn in this view.
	}
	Record record = table.records[member.x];
	Bucket bucket = buckets.data[member.y];
	uint command = member_command(record, bucket, params.lod_threshold, params.lod_distance_multiplier, params.lod_camera, params.view_flags);
	// Rounding bound: the CPU matrix bound times the largest world magnitude
	// of the box (as the MultiMesh test).
	vec3 magnitude = max(abs(record.aabb_position), abs(record.aabb_position + record.aabb_size)) + 1.0;
	vec4 error = (occlusion.abs_to_clip * vec4(magnitude, 1.0)) * (128.0 * 1.1920929e-7);
	bool occluded = hzb_box_occluded(record.aabb_position, record.aabb_size, occlusion.world_to_clip, error, ivec2(occlusion.size.xy));
	if (bool(params.view_flags & VIEW_FLAG_TWO_PASS)) {
		// The instance's surfaces share its box: they write the same value.
		history.data[member.x] = occluded ? 1u : 0u;
		if (phase1.data[index] == 0u) {
			atomicAdd(statistics.data[STAT_GPU_INSTANCES_DEPTH_DEFERRED], 1u);
			if (!occluded) {
				append_phase(index, bucket, command);
			}
		}
	}
	if (occluded) {
		atomicAdd(statistics.data[STAT_GPU_INSTANCES_OCCLUDED], 1u);
		return;
	}
	uint rank = append_member(0, bucket.command + command);
	ids.data[bucket.ids_first + command * bucket.capacity + rank] = index;
}

#else

// Culls one member; returns the render info objects it adds.
uint cull_member(uint index, out uint r_primitives) {
	r_primitives = 0;
	if (index >= params.member_count) {
		return 0;
	}
	uvec4 member = members.data[index];
	if (member.x == 0xFFFFFFFFu) {
		return 0; // A free member slot.
	}
#ifdef MODE_SHADOW_BATCH
	ShadowPass pass = shadow_passes.data[params.pass_first + gl_WorkGroupID.y];
	// Most members cast into few passes: test before reading the record.
	if ((visible.bits[pass.visible_offset + (member.x >> 5)] & (1u << (member.x & 31u))) == 0) {
		return 0;
	}
#endif
	Record record = table.records[member.x];
#ifdef MODE_SHADOW_BATCH
	// Casters whose shadow reaches no receiver the camera sees.
	if (pass.receiver_grid != 0xFFFFFFFFu) {
		atomicAdd(group_receivers_tested, 1u);
		if (!receivers_reachable(pass.receiver_grid, record.aabb_position, record.aabb_size)) {
			// What the pass would have drawn: the measure of the mask's worth.
			Bucket culled_bucket = buckets.data[member.y];
			uint culled_command = member_command(record, culled_bucket, pass.lod_threshold, pass.lod_distance_multiplier, pass.lod_camera, pass.view_flags);
			atomicAdd(group_receivers_culled, command_primitives.data[culled_bucket.command + culled_command]);
			return 0;
		}
	}
#endif
	Bucket bucket = buckets.data[member.y];
#ifdef MODE_SHADOW_BATCH
	uint view_flags = pass.view_flags;
	float lod_threshold = pass.lod_threshold;
	float lod_distance_multiplier = pass.lod_distance_multiplier;
	vec3 lod_camera = pass.lod_camera;
	uint command_offset = pass.command_offset;
	uint ids_offset = pass.ids_offset;
#else
	uint view_flags = params.view_flags;
	float lod_threshold = params.lod_threshold;
	float lod_distance_multiplier = params.lod_distance_multiplier;
	vec3 lod_camera = params.lod_camera;
	uint command_offset = 0;
	uint ids_offset = 0;
#endif
	bool moved = bool(record.gpu_flags & GPU_FLAG_MOVED);
	bool motion_pass = bool(view_flags & VIEW_FLAG_MOTION);

#ifndef MODE_SHADOW_BATCH
	// The scene cull's answer: frustum, layers, occluders (shadow passes
	// tested it above).
	if ((visible.bits[member.x >> 5] & (1u << (member.x & 31u))) == 0) {
		return 0;
	}
#endif
	uint command = member_command(record, bucket, lod_threshold, lod_distance_multiplier, lod_camera, view_flags);
	uint rank = append_member(command_offset, bucket.command + command);
	ids.data[ids_offset + bucket.ids_first + command * bucket.capacity + rank] = index;
	if (bucket.depth_command != 0xFFFFFFFFu) {
		// A separate shadow mesh: the depth pre-pass draws as many instances.
		append_member(command_offset, bucket.depth_command + command);
	}
#ifndef MODE_SHADOW_BATCH
	if (bool(view_flags & VIEW_FLAG_TWO_PASS)) {
		bool in_phase1 = history.data[member.x] == 0u;
		phase1.data[index] = in_phase1 ? 1u : 0u;
		if (in_phase1) {
			append_phase(index, bucket, command);
		}
	}
#endif
	r_primitives = command_primitives.data[bucket.command + command];
	return motion_pass && moved ? 2 : 1;
}

void main() {
	if (gl_LocalInvocationIndex == 0) {
		group_objects = 0;
		group_primitives = 0;
#ifdef MODE_SHADOW_BATCH
		group_receivers_tested = 0;
		group_receivers_culled = 0;
#endif
	}
	barrier();
	uint primitives;
	uint objects = cull_member(gl_GlobalInvocationID.x, primitives);
	if (objects > 0) {
		atomicAdd(group_objects, objects);
		atomicAdd(group_primitives, primitives);
	}
	barrier();
	if (gl_LocalInvocationIndex == 0 && group_objects > 0) {
		atomicAdd(stats.objects, group_objects);
		atomicAdd(stats.primitives, group_primitives);
	}
#ifdef MODE_SHADOW_BATCH
	if (gl_LocalInvocationIndex == 0 && group_receivers_tested > 0) {
		atomicAdd(stats.receivers_tested, group_receivers_tested);
		atomicAdd(stats.receivers_culled, group_receivers_culled);
	}
#endif
}

#endif
