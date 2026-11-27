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
	uvec4 history; // First history word of the surface, the surface's draw index (its count slot).
};
layout(set = 0, binding = 1, std430) readonly buffer Inputs {
	Input data[];
}
inputs;

// Per group, from the surface's first group (command offset / 5): the first
// index of a selected group, UINT32_MAX otherwise (mesh_clod_select.glsl).
layout(set = 0, binding = 2, std430) readonly buffer SelectionWords {
	uint data[];
}
selection;

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
layout(set = 0, binding = 6, std430) writeonly buffer PassCommands {
	uint data[];
}
pass_commands;

#ifndef MODE_SPLIT
// The colour pass commands: selected groups not proven hidden.
layout(set = 0, binding = 7, std430) writeonly buffer ColorCommands {
	uint data[];
}
color_commands;
#endif

// Draw counts of the compact streams, one region of params.count_stride words
// per stream: selected, colour, phase-1 depth, phase-2 depth.
layout(set = 0, binding = 8, std430) buffer Counts {
	uint data[];
}
counts;

layout(push_constant, std430) uniform Params {
	uint draw_index;
	uint two_pass;
	uint compact;
	uint count_stride;
}
params;

const uint STREAM_COLOR = 1u;
const uint STREAM_PHASE1 = 2u;
const uint STREAM_PHASE2 = 3u;

// Compact streams append only drawn groups and count them; otherwise every
// group has its record, with zero instances when not drawn.
uint command_word(Input input_data, uint id, uint stream, bool drawn) {
	if (params.compact == 0u) {
		return input_data.params.y + id * 5u;
	}
	if (!drawn) {
		return 0xffffffffu;
	}
	return input_data.params.y + atomicAdd(counts.data[stream * params.count_stride + input_data.history.y], 1u) * 5u;
}

void write_pass_command(uint word, Group group, uint first_index, bool drawn) {
	if (word != 0xffffffffu) {
		pass_commands.data[word] = group.draw.y;
		pass_commands.data[word + 1u] = drawn ? 1u : 0u;
		pass_commands.data[word + 2u] = drawn ? first_index : 0u;
		pass_commands.data[word + 3u] = 0u;
		pass_commands.data[word + 4u] = 0u;
	}
}

#ifndef MODE_SPLIT
void write_color_command(uint word, Group group, uint first_index, bool drawn) {
	if (word != 0xffffffffu) {
		color_commands.data[word] = group.draw.y;
		color_commands.data[word + 1u] = drawn ? 1u : 0u;
		color_commands.data[word + 2u] = drawn ? first_index : 0u;
		color_commands.data[word + 3u] = 0u;
		color_commands.data[word + 4u] = 0u;
	}
}
#endif

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
	uint history_word = input_data.history.x + id;
	uint first_index = selection.data[input_data.params.y / 5u + id];
	bool selected = first_index != 0xffffffffu;
	if (params.compact != 0u && !selected) {
		return;
	}
	Group group = groups.data[id];

#ifdef MODE_SPLIT
	// Phase 1 draws the selected groups that were visible when last tested.
	bool deferred = selected && history.data[history_word] != 0u;
#ifdef GEOMETRY_STATISTICS
	if (deferred) {
		atomicAdd(statistics.data[STAT_HZB_GROUPS_DEFERRED], 1u);
		atomicAdd(statistics.data[STAT_HZB_TRIANGLES_DEFERRED], group.draw.y / 3u);
	}
#endif
	bool drawn = selected && !deferred;
	uint word = command_word(input_data, id, STREAM_PHASE1, drawn);
	write_pass_command(word, group, first_index, drawn);
#else
	bool deferred = params.two_pass != 0u && history.data[history_word] != 0u;
	bool occluded = false;
	if (selected) {
		occluded = is_occluded(input_data, group);
		history.data[history_word] = occluded ? 1u : 0u;
#ifdef GEOMETRY_STATISTICS
		if (occluded) {
			atomicAdd(statistics.data[STAT_HZB_GROUPS_OCCLUDED], 1u);
			atomicAdd(statistics.data[STAT_HZB_TRIANGLES_OCCLUDED], group.draw.y / 3u);
		} else if (deferred) {
			atomicAdd(statistics.data[STAT_HZB_GROUPS_RECOVERED], 1u);
			atomicAdd(statistics.data[STAT_HZB_TRIANGLES_RECOVERED], group.draw.y / 3u);
		}
#endif
	}
	bool drawn = selected && !occluded;
	uint word = command_word(input_data, id, STREAM_COLOR, drawn);
	write_color_command(word, group, first_index, drawn);
	if (params.two_pass != 0u) {
		// Occluded last frame, visible against this frame's phase-1 depth:
		// drawn into depth before anything reads it.
		bool recovered = drawn && deferred;
		uint phase2_word = command_word(input_data, id, STREAM_PHASE2, recovered);
		write_pass_command(phase2_word, group, first_index, recovered);
	}
#endif
}
