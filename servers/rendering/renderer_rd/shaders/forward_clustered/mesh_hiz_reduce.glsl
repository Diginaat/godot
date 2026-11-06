#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(r32f, set = 0, binding = 1) uniform restrict writeonly image2D target_depth;

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 target_size;
}
params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, params.target_size))) {
		return;
	}
	float depth = 0.0;
#ifdef MODE_COPY
	// Padding is reverse-Z clear depth: missing pixels cannot prove occlusion.
	if (all(lessThan(pixel, params.source_size))) {
		depth = texelFetch(source_depth, pixel, 0).r;
	}
#else
	depth = 1.0;
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			ivec2 sample_pixel = min(pixel * 2 + ivec2(x, y), params.source_size - 1);
			depth = min(depth, texelFetch(source_depth, sample_pixel, 0).r);
		}
	}
#endif
	// NaNs must never become occluders. Reverse-Z uses the farthest (minimum)
	// covered depth, unlike the nearest-depth pyramid used for SSR ray marching.
	depth = (isnan(depth) || isinf(depth)) ? 0.0 : clamp(depth, 0.0, 1.0);
	imageStore(target_depth, pixel, vec4(depth));
}
