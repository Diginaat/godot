#[compute]

#version 450

#VERSION_DEFINES

// LEVELS (1 to 4) pyramid levels per dispatch: target_0 is reduced from
// source_depth, each next level from the one before in shared memory. One
// dispatch per four levels instead of one per level: in small scenes the
// barriers between the dispatches were most of the pyramid's GPU time.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(r32f, set = 0, binding = 1) uniform restrict writeonly image2D target_0;
#if LEVELS > 1
layout(r32f, set = 0, binding = 2) uniform restrict writeonly image2D target_1;
#endif
#if LEVELS > 2
layout(r32f, set = 0, binding = 3) uniform restrict writeonly image2D target_2;
#endif
#if LEVELS > 3
layout(r32f, set = 0, binding = 4) uniform restrict writeonly image2D target_3;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 target_size; // Of target_0.
}
params;

#if LEVELS > 1
// This group's 8 x 8 texels of target_0, then the 4 x 4, 2 x 2 and 1 x 1
// texels of the next levels (row stride 8).
shared float tile[64];

void store_level(int p_level, ivec2 p_pixel, float p_depth) {
	if (p_level == 1) {
		imageStore(target_1, p_pixel, vec4(p_depth));
	}
#if LEVELS > 2
	if (p_level == 2) {
		imageStore(target_2, p_pixel, vec4(p_depth));
	}
#endif
#if LEVELS > 3
	if (p_level == 3) {
		imageStore(target_3, p_pixel, vec4(p_depth));
	}
#endif
}
#endif

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	float depth = 0.0;
	if (all(lessThan(pixel, params.target_size))) {
		depth = 1.0;
#ifdef MODE_FIRST
		// The first level, at half the depth buffer's resolution: the farthest
		// of each 2 x 2 pixels. Padding is reverse-Z clear depth: missing pixels
		// cannot prove occlusion.
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < 2; x++) {
				ivec2 sample_pixel = pixel * 2 + ivec2(x, y);
				depth = min(depth, all(lessThan(sample_pixel, params.source_size)) ? texelFetch(source_depth, sample_pixel, 0).r : 0.0);
			}
		}
#else
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
		imageStore(target_0, pixel, vec4(depth));
	}

#if LEVELS > 1
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	tile[local.y * 8 + local.x] = depth;
	// Level sizes are powers of two (the pyramid is padded), so a group's
	// tile is either whole or, when the level is smaller than the tile, the
	// only one and starts at 0: the level's own size bounds it.
	ivec2 size = params.target_size;
	for (int level = 1; level < LEVELS; level++) {
		ivec2 valid = min(size, ivec2(8 >> (level - 1)));
		ivec2 next_size = max(size >> 1, ivec2(1));
		bool reducing = all(lessThan(local, min(next_size, ivec2(8 >> level))));
		memoryBarrierShared();
		barrier();
		float reduced = 1.0;
		if (reducing) {
			// Clamped like the per-level reduction: a 1-texel side repeats.
			for (int y = 0; y < 2; y++) {
				for (int x = 0; x < 2; x++) {
					ivec2 t = min(local * 2 + ivec2(x, y), valid - 1);
					reduced = min(reduced, tile[t.y * 8 + t.x]);
				}
			}
		}
		memoryBarrierShared();
		barrier();
		if (reducing) {
			tile[local.y * 8 + local.x] = reduced;
			store_level(level, ivec2(gl_WorkGroupID.xy) * (8 >> level) + local, reduced);
		}
		size = next_size;
	}
#endif
}
