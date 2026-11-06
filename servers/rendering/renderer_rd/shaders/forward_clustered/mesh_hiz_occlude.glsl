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
};
layout(set = 0, binding = 1, std430) readonly buffer Inputs {
	Input data[];
}
inputs;

layout(set = 0, binding = 2, std430) buffer Commands {
	uint data[];
}
commands;

layout(set = 0, binding = 3) uniform sampler2D depth_pyramid;

#ifdef GEOMETRY_STATISTICS
// Viewport counters (word layout in geometry_statistics.cpp), read back
// asynchronously only while requested.
layout(set = 0, binding = 4, std430) buffer Statistics {
	uint data[];
}
statistics;
#endif

layout(push_constant, std430) uniform Params {
	uint draw_index;
}
params;

void main() {
	Input input_data = inputs.data[params.draw_index];
	uint id = gl_GlobalInvocationID.x;
	if (id >= input_data.params.x) {
		return;
	}
	uint command = input_data.params.y + id * 5u;
	if (commands.data[command + 1u] == 0u) {
		return;
	}
	Group group = groups.data[id];
	vec2 uv_min = vec2(1.0);
	vec2 uv_max = vec2(0.0);
	float nearest_depth = 0.0;
	vec4 error = input_data.clip_error;
	if (any(isnan(error)) || any(isinf(error))) {
		return;
	}
	for (uint corner = 0u; corner < 8u; corner++) {
		vec3 point = group.position_error.xyz + group.size.xyz * vec3(float(corner & 1u), float((corner >> 1u) & 1u), float((corner >> 2u) & 1u));
		vec4 clip = input_data.local_to_clip * vec4(point, 1.0);
		if (any(isnan(clip)) || any(isinf(clip)) || clip.w - error.w <= 0.0001) {
			return;
		}
		vec3 a = (clip.xyz - error.xyz) / (clip.w - error.w);
		vec3 b = (clip.xyz - error.xyz) / (clip.w + error.w);
		vec3 c = (clip.xyz + error.xyz) / (clip.w - error.w);
		vec3 d = (clip.xyz + error.xyz) / (clip.w + error.w);
		if (any(isnan(a)) || any(isinf(a)) || any(isnan(b)) || any(isinf(b)) || any(isnan(c)) || any(isinf(c)) || any(isnan(d)) || any(isinf(d))) {
			return;
		}
		vec3 ndc_min = min(min(a, b), min(c, d));
		vec3 ndc_max = max(max(a, b), max(c, d));
		// Clipped/near-plane-crossing bounds must remain visible. No corner-only
		// projection is trusted for a box crossing the eye or near/far clip plane.
		if (ndc_min.z < 0.0 || ndc_max.z > 1.0) {
			return;
		}
		uv_min = min(uv_min, ndc_min.xy * 0.5 + 0.5);
		uv_max = max(uv_max, ndc_max.xy * 0.5 + 0.5);
		nearest_depth = max(nearest_depth, ndc_max.z);
	}
	if (any(lessThan(uv_min, vec2(0.0))) || any(greaterThan(uv_max, vec2(1.0)))) {
		return;
	}
	ivec2 size = ivec2(input_data.params.zw);
	ivec2 first = max(ivec2(floor(uv_min * vec2(size))) - 1, ivec2(0));
	ivec2 last = min(ivec2(ceil(uv_max * vec2(size))) + 1, size - 1);
	int level = 0;
	while (any(greaterThan((last >> level) - (first >> level), ivec2(1)))) {
		level++;
	}
	ivec2 low = first >> level;
	ivec2 high = last >> level;
	float farthest_occluder = 1.0;
	for (int y = low.y; y <= high.y; y++) {
		for (int x = low.x; x <= high.x; x++) {
			farthest_occluder = min(farthest_occluder, texelFetch(depth_pyramid, ivec2(x, y), level).r);
		}
	}
	if (nearest_depth + 0.00001 < farthest_occluder) {
		commands.data[command + 1u] = 0u;
#ifdef GEOMETRY_STATISTICS
		atomicAdd(statistics.data[6], 1u);
		atomicAdd(statistics.data[7], group.draw.y / 3u);
#endif
	}
}
