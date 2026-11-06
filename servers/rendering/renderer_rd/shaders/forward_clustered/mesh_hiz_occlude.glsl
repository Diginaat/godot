#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Group {
	vec4 position_error;
	vec4 size;
	uvec4 draw;
};
layout(set = 0, binding = 0, std430) readonly buffer Groups {
	Group data[];
}
groups;

struct Input {
	mat4 local_to_clip;
	vec4 clip_error; // Conservative float-operation bound, including world-coordinate cancellation.
	uvec4 params; // Group count, command offset in words, actual depth width/height.
	uvec4 history; // First history word of the surface.
};
layout(set = 0, binding = 1, std430) readonly buffer Inputs {
	Input data[];
}
inputs;

layout(set = 0, binding = 2, std430) buffer Commands {
	uint data[];
}
commands;

#ifndef MODE_SPLIT
layout(set = 0, binding = 3) uniform sampler2D depth_pyramid;
#endif

#ifdef GEOMETRY_STATISTICS
// Viewport counters (word layout in geometry_statistics.cpp), read back
// asynchronously only while requested.
layout(set = 0, binding = 4, std430) buffer Statistics {
	uint data[];
}
statistics;
#endif

// One word per group and viewport: nonzero if the group was occluded the last
// time it was tested. Only a scheduling hint: a wrong value can cost a phase-2
// draw, never a missing group.
layout(set = 0, binding = 5, std430) buffer History {
	uint data[];
}
history;

// MODE_SPLIT: the phase-1 depth commands. Otherwise: the phase-2 depth commands
// (groups occluded last frame that are visible now).
layout(set = 0, binding = 6, std430) buffer PassCommands {
	uint data[];
}
pass_commands;

layout(push_constant, std430) uniform Params {
	uint draw_index;
	uint two_pass;
	uint pad0;
	uint pad1;
}
params;

// Word layout of the statistics buffer (see geometry_statistics.cpp).
const uint STAT_HZB_GROUPS_OCCLUDED = 6u;
const uint STAT_HZB_TRIANGLES_OCCLUDED = 7u;
const uint STAT_HZB_GROUPS_DEFERRED = 24u;
const uint STAT_HZB_TRIANGLES_DEFERRED = 25u;
const uint STAT_HZB_GROUPS_RECOVERED = 26u;
const uint STAT_HZB_TRIANGLES_RECOVERED = 27u;

#ifndef MODE_SPLIT
#include "mesh_hiz_inc.glsl"

bool is_occluded(Input input_data, Group group) {
	return hzb_box_occluded(group.position_error.xyz, group.size.xyz, input_data.local_to_clip, input_data.clip_error, ivec2(input_data.params.zw));
}
#endif

void main() {
	Input input_data = inputs.data[params.draw_index];
	uint id = gl_GlobalInvocationID.x;
	if (id >= input_data.params.x) {
		return;
	}
	uint command = input_data.params.y + id * 5u;
	uint history_word = input_data.history.x + id;
	bool selected = commands.data[command + 1u] != 0u;

#ifdef MODE_SPLIT
	// Phase 1 draws the selected groups that were visible when last tested.
	for (uint i = 0u; i < 5u; i++) {
		pass_commands.data[command + i] = commands.data[command + i];
	}
	if (selected && history.data[history_word] != 0u) {
		pass_commands.data[command + 1u] = 0u;
#ifdef GEOMETRY_STATISTICS
		atomicAdd(statistics.data[STAT_HZB_GROUPS_DEFERRED], 1u);
		atomicAdd(statistics.data[STAT_HZB_TRIANGLES_DEFERRED], groups.data[id].draw.y / 3u);
#endif
	}
#else
	bool deferred = params.two_pass != 0u && history.data[history_word] != 0u;
	if (params.two_pass != 0u) {
		for (uint i = 0u; i < 5u; i++) {
			pass_commands.data[command + i] = commands.data[command + i];
		}
		pass_commands.data[command + 1u] = 0u;
	}
	if (!selected) {
		return;
	}
	Group group = groups.data[id];
	if (is_occluded(input_data, group)) {
		commands.data[command + 1u] = 0u;
		history.data[history_word] = 1u;
#ifdef GEOMETRY_STATISTICS
		atomicAdd(statistics.data[STAT_HZB_GROUPS_OCCLUDED], 1u);
		atomicAdd(statistics.data[STAT_HZB_TRIANGLES_OCCLUDED], group.draw.y / 3u);
#endif
		return;
	}
	history.data[history_word] = 0u;
	if (deferred) {
		// Occluded last frame, visible against this frame's phase-1 depth:
		// drawn into depth before anything reads it.
		pass_commands.data[command + 1u] = commands.data[command + 1u];
#ifdef GEOMETRY_STATISTICS
		atomicAdd(statistics.data[STAT_HZB_GROUPS_RECOVERED], 1u);
		atomicAdd(statistics.data[STAT_HZB_TRIANGLES_RECOVERED], group.draw.y / 3u);
#endif
	}
#endif
}
