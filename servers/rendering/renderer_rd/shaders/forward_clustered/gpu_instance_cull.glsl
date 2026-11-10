#[compute]

#version 450

#VERSION_DEFINES

// GPU path for eligible instances (docs/renderer, Phase 2). One thread per
// bucket member (an instance surface): frustum and layer test from the GPU
// instance table, the index LOD as the CPU render list chooses it, then the
// instance data Forward+ draws with, at the member's own index, and the
// member's index appended to its bucket level's draw. The CPU zeroed each
// level's instance count.

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
	uint count;
	uint lod_count;
	uint lod_edges_first;
	uint depth_command; // 0xFFFFFFFF: the depth pre-pass draws with command.
	uint pad0;
	uint pad1;
	uint pad2;
};

struct InstanceData { // SceneState::InstanceData, 176 bytes.
	vec4 transform[3];
	vec4 compressed_aabb_position_pad;
	vec3 compressed_aabb_size;
	uint material_feedback_index;
	vec4 uv_scale;
	uint flags;
	uint instance_uniforms_ofs;
	uint gi_offset;
	uint layer_mask;
	vec4 prev_transform[3];
	vec4 lightmap_uv_scale;
};

layout(set = 0, binding = 0, std430) restrict readonly buffer Table {
	Record records[];
}
table;

layout(set = 0, binding = 1, std430) restrict readonly buffer Members {
	uvec2 data[]; // Table slot, bucket.
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

layout(set = 0, binding = 4, std430) restrict writeonly buffer Output {
	InstanceData data[]; // One per member.
}
output_instances;

layout(set = 0, binding = 5, std430) restrict writeonly buffer IDs {
	uint data[];
}
ids;

layout(set = 0, binding = 6, std430) restrict readonly buffer LODEdges {
	float data[];
}
lod_edges;

#define VIEW_FLAG_USE_GI_BUFFERS 1
#define VIEW_FLAG_USE_SDFGI 2
#define VIEW_FLAG_ORTHOGONAL 4

#define GPU_FLAG_MOVED 1
#define GPU_FLAG_CAN_SDFGI 2

// Same values as the instance flags in scene_forward_clustered_inc.glsl
// (static_assert in gpu_instances.cpp).
#define INSTANCE_FLAGS_DYNAMIC (1 << 3)
#define INSTANCE_FLAGS_USE_GI_BUFFERS (1 << 5)
#define INSTANCE_FLAGS_USE_SDFGI (1 << 6)
#define INSTANCE_FLAGS_FADE_SHIFT 24
#define INSTANCE_FLAGS_FADE_MASK (0xFFu << INSTANCE_FLAGS_FADE_SHIFT)

layout(push_constant, std430) uniform Params {
	vec4 planes[6]; // World space, normal pointing out, w = distance.
	uint member_count;
	uint visible_layers;
	uint view_flags;
	float lod_threshold; // Screen mesh LOD threshold; 0 or less: no LODs.
	vec3 lod_camera;
	float lod_distance_multiplier;
}
params;

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= params.member_count) {
		return;
	}
	uvec2 member = members.data[index];
	Record record = table.records[member.x];
	if ((record.layer_mask & params.visible_layers) == 0) {
		return;
	}
	// Outside when the bounds' corner nearest to the inside lies outside a
	// plane, as in the CPU scene cull.
	vec3 aabb_min = record.aabb_position;
	vec3 aabb_max = record.aabb_position + record.aabb_size;
	for (uint i = 0; i < 6; i++) {
		vec3 normal = params.planes[i].xyz;
		vec3 nearest = vec3(normal.x > 0.0 ? aabb_min.x : aabb_max.x, normal.y > 0.0 ? aabb_min.y : aabb_max.y, normal.z > 0.0 ? aabb_min.z : aabb_max.z);
		if (dot(normal, nearest) > params.planes[i].w) {
			return;
		}
	}

	Bucket bucket = buckets.data[member.y];

	// The index LOD, as mesh_surface_get_lod() chooses it for the CPU list.
	uint level = 0;
	if (bucket.lod_count > 0 && params.lod_threshold > 0.0) {
		float distance = 1.0;
		if (!bool(params.view_flags & VIEW_FLAG_ORTHOGONAL)) {
			distance = length(max(max(vec3(0.0), aabb_min - params.lod_camera), params.lod_camera - aabb_max));
		}
		float distance_threshold = distance * params.lod_distance_multiplier;
		for (uint i = 0; i < bucket.lod_count; i++) {
			float screen_size = lod_edges.data[bucket.lod_edges_first + i] * record.lod_scale / distance_threshold;
			if (screen_size > params.lod_threshold) {
				break;
			}
			level = i + 1;
		}
	}
	uint rank = atomicAdd(commands.data[(bucket.command + level) * 5 + 1], 1);
	ids.data[bucket.ids_first + level * bucket.count + rank] = index;
	if (bucket.depth_command != 0xFFFFFFFFu) {
		// A separate shadow mesh: the depth pre-pass draws as many instances.
		atomicAdd(commands.data[(bucket.depth_command + level) * 5 + 1], 1);
	}

	// What _fill_render_list() and _fill_instance_data() write for an opaque
	// instance without lightmap, VoxelGI, fade or motion pass.
	uint flags = (record.flags & ~INSTANCE_FLAGS_FADE_MASK) | (255u << INSTANCE_FLAGS_FADE_SHIFT);
	if (bool(record.gpu_flags & GPU_FLAG_MOVED)) {
		flags |= INSTANCE_FLAGS_DYNAMIC;
	}
	if (bool(params.view_flags & VIEW_FLAG_USE_GI_BUFFERS)) {
		flags |= INSTANCE_FLAGS_USE_GI_BUFFERS;
	}
	if (bool(params.view_flags & VIEW_FLAG_USE_SDFGI) && bool(record.gpu_flags & GPU_FLAG_CAN_SDFGI)) {
		flags |= INSTANCE_FLAGS_USE_SDFGI;
	}

	InstanceData data;
	data.transform = record.transform;
	data.compressed_aabb_position_pad = vec4(bucket.compressed_aabb_position, 0.0);
	data.compressed_aabb_size = bucket.compressed_aabb_size;
	data.material_feedback_index = bucket.material_feedback_index;
	data.uv_scale = bucket.uv_scale;
	data.flags = flags;
	data.instance_uniforms_ofs = record.instance_uniforms_ofs;
	data.gi_offset = 0xFFFFFFFFu;
	data.layer_mask = record.layer_mask;
	data.prev_transform = record.prev_transform;
	data.lightmap_uv_scale = record.lightmap_uv_scale;
	output_instances.data[index] = data;
}
