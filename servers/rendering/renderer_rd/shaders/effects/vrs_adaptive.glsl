#[compute]

#version 450

#VERSION_DEFINES

#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

// Content-adaptive shading rate (Viewport VRS mode Adaptive). One workgroup
// per shading rate texel (a tile of pixels): it measures how much the
// previous frame's image changes between neighboring pixels in the tile and
// picks the coarsest rate whose error stays under a just-noticeable
// difference. The tile is read with a margin so that edges moving a few
// pixels since the last frame still get full rate.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Largest tile (32) plus the margin on both sides plus one pixel for the
// neighbor differences.
#define MARGIN 4
#define MAX_REGION (32 + 2 * MARGIN + 1)

layout(set = 0, binding = 0) uniform sampler2D source_color; // Previous frame, linear HDR.

#ifdef SPLIT_RG
layout(rg8, set = 0, binding = 1) uniform restrict writeonly image2D rate_image;
#else
layout(r8ui, set = 0, binding = 1) uniform restrict writeonly uimage2D rate_image;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 tile_size;
	float threshold;
	uint max_rate; // log2 of the coarsest rate per axis (1 = 2x2, 2 = 4x4).
	uint full_rate; // Write full rate only (unsupported tile size or no source).
	uint pad;

}
params;

shared float luma[MAX_REGION * MAX_REGION];
shared vec4 partial[64]; // Squared x and y differences, pixel count, luma sum.
shared vec2 partial_max[64]; // Largest x and y differences.

float perceptual_luma(ivec2 p) {
	vec3 color = texelFetch(source_color, clamp(p, ivec2(0), params.source_size - 1), 0).rgb;
	float l = dot(color, vec3(0.2126, 0.7152, 0.0722));
	if (isnan(l) || isinf(l)) {
		// Undefined content (new buffers): a checkerboard keeps full rate.
		return float((p.x + p.y) & 1);
	}
	l = max(l, 0.0);
	// Compress HDR, then approximate the display curve.
	return sqrt(l / (1.0 + l));
}

uint pick_rate(float rms_difference, float max_difference, float limit) {
	// Shading two pixels with one sample errs by about half their difference;
	// four pixels by about 2.13 times that (Yang et al. 2019).
	// The mean alone lets one silhouette through a flat tile go coarse (the
	// coarse sample then shades both sides of the edge); half the largest
	// difference keeps such tiles at full rate. Measured on the valley test
	// map: PSNR 36 dB with the mean alone, 47 dB with this mix.
	float half_error = 0.5 * mix(rms_difference, max_difference, 0.5);
	if (params.max_rate >= 2u && half_error * 2.13 < limit) {
		return 2u;
	}
	return half_error < limit ? 1u : 0u;
}

void main() {
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	uint local = gl_LocalInvocationIndex;
	uint rate_x = 0u;
	uint rate_y = 0u;
	if (params.full_rate == 0u) {
		ivec2 region = params.tile_size + 2 * MARGIN;
		int stride = region.x + 1;
		ivec2 origin = tile * params.tile_size - MARGIN;
		// 2D loops over the region: no integer division per pixel, which
		// some shader compilers (D3D12) turn into long sequences.
		ivec2 thread = ivec2(gl_LocalInvocationID.xy);
		for (int y = thread.y; y <= region.y; y += 8) {
			for (int x = thread.x; x < stride; x += 8) {
				luma[y * stride + x] = perceptual_luma(origin + ivec2(x, y));
			}
		}
		barrier();

		vec4 sums = vec4(0.0);
		vec2 maxima = vec2(0.0);
		for (int y = thread.y; y < region.y; y += 8) {
			for (int x = thread.x; x < region.x; x += 8) {
				ivec2 p = origin + ivec2(x, y);
				if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, params.source_size))) {
					continue;
				}
				int at = y * stride + x;
				float l = luma[at];
				float dx = luma[at + 1] - l;
				float dy = luma[at + stride] - l;
				sums += vec4(dx * dx, dy * dy, 1.0, l);
				maxima = max(maxima, abs(vec2(dx, dy)));
			}
		}
		// Subgroup sums, then one shared slot per subgroup: two barriers per
		// workgroup instead of eight (a barrier costs much more than the
		// arithmetic here, notably on D3D12).
		sums = subgroupAdd(sums);
		maxima = subgroupMax(maxima);
		if (subgroupElect()) {
			partial[gl_SubgroupID] = sums;
			partial_max[gl_SubgroupID] = maxima;
		}
		barrier();
		if (local == 0u) {
			for (uint i = 1u; i < gl_NumSubgroups; i++) {
				partial[0] += partial[i];
				partial_max[0] = max(partial_max[0], partial_max[i]);
			}
		}
		if (local == 0u && partial[0].z > 0.0) {
			float count = partial[0].z;
			// Weber-like limit: brighter areas hide larger differences; the
			// constant keeps dark areas from going coarse on noise.
			float limit = params.threshold * (partial[0].w / count + 0.05);
			rate_x = pick_rate(sqrt(partial[0].x / count), partial_max[0].x, limit);
			rate_y = pick_rate(sqrt(partial[0].y / count), partial_max[0].y, limit);
			// 4x1 and 1x4 are not valid rates.
			rate_x = min(rate_x, rate_y + 1u);
			rate_y = min(rate_y, rate_x + 1u);
		}
	}
	if (local == 0u) {
#ifdef SPLIT_RG
		// Fragment density map: 1.0 is full density.
		imageStore(rate_image, tile, vec4(1.0 / float(1u << rate_x), 1.0 / float(1u << rate_y), 0.0, 0.0));
#else
		// VK_KHR_fragment_shading_rate: log2(width) << 2 | log2(height).
		imageStore(rate_image, tile, uvec4((rate_x << 2u) | rate_y));
#endif
	}
}
