#[compute]

#version 450

#VERSION_DEFINES

// HZB occlusion of the meshes the CPU render list draws in the opaque colour
// pass (rendering/geometry/mesh_occlusion/enabled). One record per tested
// render element: its world box (the union of the instances it draws) and its
// draw arguments. The colour pass draws the element through the indirect
// command written here, with zero instances when the box is proven hidden.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Element {
	vec4 position; // World box corner.
	vec4 size; // World box size.
	uvec4 draw; // Command index, index (or vertex) count, instance count, unused.
};

layout(set = 0, binding = 0, std430) readonly buffer Elements {
	Element data[];
}
elements;

layout(set = 0, binding = 1, std430) writeonly buffer Commands {
	uint data[];
}
commands;

layout(set = 0, binding = 2, std430) readonly buffer View {
	mat4 world_to_clip;
	mat4 abs_to_clip; // abs(P) * abs(V), bounds the rounding of world_to_clip.
	uvec4 size; // Depth width, height.
}
view;

layout(set = 0, binding = 3) uniform sampler2D depth_pyramid;

// Viewport counters (word layout in geometry_statistics.cpp).
layout(set = 0, binding = 4, std430) buffer Statistics {
	uint data[];
}
statistics;

layout(push_constant, std430) uniform Params {
	uint count;
	uint pad[3];
}
params;

const uint STAT_MESHES_OCCLUDED = 30u;

#include "mesh_hiz_inc.glsl"

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= params.count) {
		return;
	}
	Element element = elements.data[index];
	// Rounding bound as the GPU instance test: the CPU matrix bound times the
	// largest world magnitude of the box.
	vec3 magnitude = max(abs(element.position.xyz), abs(element.position.xyz + element.size.xyz)) + 1.0;
	vec4 error = (view.abs_to_clip * vec4(magnitude, 1.0)) * (128.0 * 1.1920929e-7);
	bool occluded = hzb_box_occluded(element.position.xyz, element.size.xyz, view.world_to_clip, error, ivec2(view.size.xy));
	if (occluded) {
		atomicAdd(statistics.data[STAT_MESHES_OCCLUDED], 1u);
	}
	// Indexed: index count, instances, first index, vertex offset, first
	// instance. Non-indexed draws read the first four words the same way.
	uint command = element.draw.x * 5u;
	commands.data[command] = element.draw.y;
	commands.data[command + 1u] = occluded ? 0u : element.draw.z;
	commands.data[command + 2u] = 0u;
	commands.data[command + 3u] = 0u;
	commands.data[command + 4u] = 0u;
}
