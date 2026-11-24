#[compute]

#version 450

#VERSION_DEFINES

// HZB occlusion of the meshes the CPU render list draws in the opaque colour
// pass (rendering/geometry/mesh_occlusion/enabled). One record per tested
// render element: its world box (the union of the instances it draws) and its
// draw arguments. The colour pass draws the element through the indirect
// command written here, with zero instances when the box is proven hidden.
//
// Two-pass (rendering/geometry/mesh_occlusion/two_pass): before the depth
// pre-pass, MODE_SPLIT writes each element's phase-1 depth command, with its
// instances only if one of them was visible when last tested (history word 0,
// indexed by GPU instance slot). After the HZB, the test also writes the
// phase-2 depth command (visible now, not drawn in phase 1) and the history.
// Phase-1 depth is a subset of the final depth, so the test stays
// conservative whatever the history says; the history only decides how much
// phase 1 draws.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Element {
	vec4 position; // World box corner.
	vec4 size; // World box size.
	uvec4 draw; // Command index, index (or vertex) count, instance count, depth pass index (or vertex) count.
	uvec4 slots; // First entry in the slot list, number of entries (one per instance).
};

layout(set = 0, binding = 0, std430) readonly buffer Elements {
	Element data[];
}
elements;

#ifndef MODE_SPLIT
layout(set = 0, binding = 1, std430) writeonly buffer Commands {
	uint data[];
}
commands;
#endif

layout(set = 0, binding = 2, std430) readonly buffer View {
	mat4 world_to_clip;
	mat4 abs_to_clip; // abs(P) * abs(V), bounds the rounding of world_to_clip.
	uvec4 size; // Depth width, height.
}
view;

#ifndef MODE_SPLIT
layout(set = 0, binding = 3) uniform sampler2D depth_pyramid;
#endif

// Viewport counters (word layout in geometry_statistics.cpp).
layout(set = 0, binding = 4, std430) buffer Statistics {
	uint data[];
}
statistics;

layout(set = 0, binding = 5, std430) readonly buffer Slots {
	uint data[];
}
slots;

// Per GPU instance slot: 1 if occluded when last tested, 0 otherwise (also
// for slots never tested).
layout(set = 0, binding = 6, std430) buffer History {
	uint data[];
}
history;

#ifdef MODE_SPLIT
layout(set = 0, binding = 7, std430) writeonly buffer Phase1Commands {
	uint data[];
}
phase1_commands;
#else
// What the split drew in phase 1. Not the history: elements of one instance
// share its slot, so another element may have rewritten it already.
layout(set = 0, binding = 7, std430) readonly buffer Phase1Commands {
	uint data[];
}
phase1_commands;

layout(set = 0, binding = 8, std430) writeonly buffer Phase2Commands {
	uint data[];
}
phase2_commands;
#endif

layout(push_constant, std430) uniform Params {
	uint count;
	uint two_pass;
	uint pad[2];
}
params;

const uint STAT_MESHES_OCCLUDED = 30u;
const uint STAT_MESHES_DEPTH_DEFERRED = 32u;

#ifndef MODE_SPLIT
#include "mesh_hiz_inc.glsl"
#endif

#ifdef MODE_SPLIT
bool drawn_in_phase1(Element element) {
	for (uint i = 0u; i < element.slots.y; i++) {
		if (history.data[slots.data[element.slots.x + i]] == 0u) {
			return true;
		}
	}
	return false;
}
#endif

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= params.count) {
		return;
	}
	Element element = elements.data[index];
	// Indexed: index count, instances, first index, vertex offset, first
	// instance. Non-indexed draws read the first four words the same way.
	uint command = element.draw.x * 5u;
#ifdef MODE_SPLIT
	bool phase1 = drawn_in_phase1(element);
	if (!phase1) {
		atomicAdd(statistics.data[STAT_MESHES_DEPTH_DEFERRED], 1u);
	}
	phase1_commands.data[command] = element.draw.w;
	phase1_commands.data[command + 1u] = phase1 ? element.draw.z : 0u;
	phase1_commands.data[command + 2u] = 0u;
	phase1_commands.data[command + 3u] = 0u;
	phase1_commands.data[command + 4u] = 0u;
#else
	// Rounding bound as the GPU instance test: the CPU matrix bound times the
	// largest world magnitude of the box.
	vec3 magnitude = max(abs(element.position.xyz), abs(element.position.xyz + element.size.xyz)) + 1.0;
	vec4 error = (view.abs_to_clip * vec4(magnitude, 1.0)) * (128.0 * 1.1920929e-7);
	bool occluded = hzb_box_occluded(element.position.xyz, element.size.xyz, view.world_to_clip, error, ivec2(view.size.xy));
	if (occluded) {
		atomicAdd(statistics.data[STAT_MESHES_OCCLUDED], 1u);
	}
	commands.data[command] = element.draw.y;
	commands.data[command + 1u] = occluded ? 0u : element.draw.z;
	commands.data[command + 2u] = 0u;
	commands.data[command + 3u] = 0u;
	commands.data[command + 4u] = 0u;
	if (params.two_pass != 0u) {
		bool phase1 = phase1_commands.data[command + 1u] != 0u;
		phase2_commands.data[command] = element.draw.w;
		phase2_commands.data[command + 1u] = (!occluded && !phase1) ? element.draw.z : 0u;
		phase2_commands.data[command + 2u] = 0u;
		phase2_commands.data[command + 3u] = 0u;
		phase2_commands.data[command + 4u] = 0u;
	}
	// Elements sharing an instance (its surfaces, or repeats with different
	// boxes) may write different values; any value is safe (see top).
	for (uint i = 0u; i < element.slots.y; i++) {
		history.data[slots.data[element.slots.x + i]] = occluded ? 1u : 0u;
	}
#endif
}
