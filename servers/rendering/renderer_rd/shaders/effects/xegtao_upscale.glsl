#[compute]

#version 450

#VERSION_DEFINES

// XeGTAO half resolution: depth-guided upscale of the denoised visibility
// (and bent normals) to the internal resolution. Each half resolution texel
// stands for the top-left pixel of its 2x2 block (see xegtao_prefilter.glsl).
// Bilinear weights are multiplied by how close the texel's depth is to the
// full resolution pixel's depth, so AO doesn't bleed across silhouettes.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "xegtao_inc.glsl"

layout(set = 0, binding = 0) uniform sampler2D source_visibility; // AO resolution.
layout(set = 0, binding = 1) uniform sampler2D source_view_depth; // AO resolution view depth (MIP 0).
layout(set = 0, binding = 2) uniform sampler2D source_depth; // Raw depth buffer, internal resolution.
#ifdef BENT_NORMALS
layout(set = 0, binding = 3) uniform sampler2D source_bent_normal; // AO resolution.
#endif

layout(r8, set = 1, binding = 0) uniform restrict writeonly image2D dest_visibility;
#ifdef BENT_NORMALS
layout(rgba8, set = 1, binding = 1) uniform restrict writeonly image2D dest_bent_normal;
#endif

void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.full_size))) {
		return;
	}

	float depth = xegtao_clamp_depth(xegtao_raw_to_view_depth(texelFetch(source_depth, pix_coord, 0).r));

	// Position in AO texels: texel p covers full pixel 2p.
	vec2 pos = vec2(pix_coord) * 0.5;
	ivec2 base = ivec2(floor(pos));
	vec2 f = pos - vec2(base);

	float sum_weight = 0.0;
	float visibility = 0.0;
	vec4 bent = vec4(0.0);
	float best_diff = 1e20;
	float best_visibility = 1.0;
	vec4 best_bent = vec4(0.5, 0.5, 1.0, 1.0);

	for (int i = 0; i < 4; i++) {
		ivec2 offset = ivec2(i & 1, i >> 1);
		ivec2 coord = clamp(base + offset, ivec2(0), params.size - 1);
		float bilinear = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
		float sample_depth = texelFetch(source_view_depth, coord, 0).r;
		float diff = abs(sample_depth - depth) / max(depth, 1e-4);
		// Relative depth tolerance: 2% of the distance passes fully.
		float depth_weight = 1.0 / (1.0 + diff * diff * 2500.0);
		float w = max(bilinear, 0.02) * depth_weight;

		float v = texelFetch(source_visibility, coord, 0).r;
		visibility += v * w;
#ifdef BENT_NORMALS
		vec4 b = texelFetch(source_bent_normal, coord, 0);
		bent += b * w;
#endif
		sum_weight += w;
		if (diff < best_diff) {
			best_diff = diff;
			best_visibility = v;
#ifdef BENT_NORMALS
			best_bent = texelFetch(source_bent_normal, coord, 0);
#endif
		}
	}

	if (sum_weight > 1e-3) {
		visibility /= sum_weight;
		bent /= sum_weight;
	} else {
		// No texel on this surface: the closest one in depth.
		visibility = best_visibility;
		bent = best_bent;
	}

	imageStore(dest_visibility, pix_coord, vec4(visibility));
#ifdef BENT_NORMALS
	vec3 n = normalize(bent.xyz * 2.0 - 1.0);
	imageStore(dest_bent_normal, pix_coord, vec4(n * 0.5 + 0.5, visibility));
#endif
}
