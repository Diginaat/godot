#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// One group owns a complete replacement for its children. Indices continue
// referencing the native source vertex buffers; no material/vertex ABI change.
struct Group {
	vec4 position_error;
	vec4 size;
	uvec4 draw; // First index, index count, parent (UINT32_MAX for roots), child count.
};

layout(set = 0, binding = 0, std430) readonly buffer Groups {
	Group data[];
}
groups;

struct Selection {
	vec4 planes[6]; // Outward world-frustum covectors pulled into source space.
	vec4 camera_projection; // Local camera xyz; pixel projection scale times model error scale.
	vec4 forward_near; // World camera forward pulled into local space; positive near clamp.
	uvec4 params; // Group count, command offset in words, orthographic flag, error pixels (float bits).
};

layout(set = 0, binding = 1, std430) readonly buffer Inputs {
	Selection data[];
}
inputs;

layout(set = 0, binding = 2, std430) writeonly buffer Commands {
	uint data[];
}
commands;

#ifdef MODE_RESIDENCY
// Header entry: x=whole-surface coverage. Following entries: x=resident bit 0,
// covered-by-self-or-descendants bit 1; y=first child-list entry; z=first index
// of the group's resident copy in the bound index buffer (the streaming pool).
// Host constructs an immutable coherent snapshot and must retain source
// fallback if incomplete.
layout(set = 0, binding = 3, std430) readonly buffer Residency {
	uvec4 data[];
}
residency;
layout(set = 0, binding = 4, std430) readonly buffer Children {
	uint data[];
}
children;
// Words from params.request_offset: word 0 requests whole-surface fallback;
// word 1+group: request bit 0, used bit 1.
layout(set = 0, binding = 5, std430) buffer Requests {
	uint data[];
}
requests;
#endif

#ifdef GEOMETRY_STATISTICS
// Viewport counters (word layout in geometry_statistics.cpp), read back
// asynchronously only while requested.
layout(set = 0, binding = 6, std430) buffer Statistics {
	uint data[];
}
statistics;
#endif
layout(push_constant, std430) uniform Params {
	uint draw_index;
#ifdef MODE_RESIDENCY
	uint request_offset; // This surface's first word in the shared request buffer.
#endif
}
params;

bool refine(Group group, Selection input_data) {
	if (group.draw.w == 0u) {
		return false;
	}
	float depth = 1.0;
	if (input_data.params.z == 0u) {
		vec3 half_extent = group.size.xyz * 0.5;
		vec3 center = group.position_error.xyz + half_extent;
		depth = max(dot(input_data.forward_near.xyz, center - input_data.camera_projection.xyz) - dot(abs(input_data.forward_near.xyz), half_extent), input_data.forward_near.w);
	}
	float projected = group.position_error.w * input_data.camera_projection.w / depth;
	// Overflow/NaN must refine conservatively rather than remove a subtree.
	return !(projected <= uintBitsToFloat(input_data.params.w));
}

#ifdef MODE_RESIDENCY
bool children_covered(uint id, Group group) {
	if (group.draw.w == 0u) {
		return false;
	}
	for (uint c = 0u; c < group.draw.w; c++) {
		uint child = children.data[residency.data[id + 1u].y + c];
		if ((residency.data[child + 1u].x & 2u) == 0u) {
			return false;
		}
	}
	return true;
}

bool resident_refine(uint id, Group group, Selection input_data) {
	return (refine(group, input_data) || (residency.data[id + 1u].x & 1u) == 0u) && children_covered(id, group);
}
#endif

void main() {
	Selection input_data = inputs.data[params.draw_index];
	uint id = gl_GlobalInvocationID.x;
#ifdef GEOMETRY_STATISTICS
	if (gl_LocalInvocationIndex == 0u && id < input_data.params.x) {
		atomicAdd(statistics.data[2], min(64u, input_data.params.x - id));
	}
#endif
	if (id >= input_data.params.x) {
		return;
	}
	Group group = groups.data[id];
#ifdef MODE_RESIDENCY
	bool reachable = residency.data[0].x != 0u && (residency.data[id + 1u].x & 2u) != 0u;
	uint parent = group.draw.z;
	while (reachable && parent != 0xffffffffu) {
		Group ancestor = groups.data[parent];
		reachable = resident_refine(parent, ancestor, input_data);
		parent = ancestor.draw.z;
	}
	bool selected = reachable && !resident_refine(id, group, input_data);
#else
	bool selected = !refine(group, input_data);
	// Ancestor veto is equivalent to exclusive root traversal, without a local
	// stack or overflow-induced holes. Cache validation guarantees parent > child.
	uint parent = group.draw.z;
	while (selected && parent != 0xffffffffu) {
		Group ancestor = groups.data[parent];
		selected = refine(ancestor, input_data);
		parent = ancestor.draw.z;
	}
#endif
	bool visible = true;
	if (selected
#ifdef MODE_RESIDENCY
			|| reachable
#endif
	) {
		vec3 half_extent = group.size.xyz * 0.5;
		vec3 center = group.position_error.xyz + half_extent;
		for (uint p = 0u; p < 6u; p++) {
			vec4 plane = input_data.planes[p];
			float distance = dot(plane.xyz, center) - dot(abs(plane.xyz), half_extent);
			float margin = 0.0001 * (1.0 + abs(plane.w) + dot(abs(plane.xyz), abs(center) + half_extent));
			if (distance > plane.w + margin) {
				visible = false;
				break;
			}
		}
	}
#ifdef MODE_RESIDENCY
	if (residency.data[0].x == 0u) {
		atomicOr(requests.data[params.request_offset], 1u);
	} else if (reachable && visible) {
		if (refine(group, input_data) && !children_covered(id, group)) {
			for (uint c = 0u; c < group.draw.w; c++) {
				uint child = children.data[residency.data[id + 1u].y + c];
				if ((residency.data[child + 1u].x & 2u) == 0u) {
					atomicOr(requests.data[params.request_offset + child + 1u], 1u);
				}
			}
		}
		if (selected) {
			atomicOr(requests.data[params.request_offset + id + 1u], 2u);
		}
	}
#endif
#ifdef GEOMETRY_STATISTICS
	if (selected) {
		atomicAdd(statistics.data[3], 1u);
		if (visible) {
			atomicAdd(statistics.data[4], 1u);
			atomicAdd(statistics.data[5], group.draw.y / 3u);
		}
	}
#endif
	uint command = input_data.params.y + id * 5u;
	commands.data[command] = group.draw.y;
	commands.data[command + 1u] = selected && visible ? 1u : 0u;
#ifdef MODE_RESIDENCY
	commands.data[command + 2u] = residency.data[id + 1u].z;
#else
	commands.data[command + 2u] = group.draw.x;
#endif
	commands.data[command + 3u] = 0u;
	commands.data[command + 4u] = 0u;
}
