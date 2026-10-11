#[compute]

#version 450

#VERSION_DEFINES

// XeGTAO pass 1: view space depth and its MIP chain (5 levels), from the
// depth buffer. Each thread handles a 2x2 block; a group 16x16 pixels.
// Based on XeGTAO_PrefilterDepths16x16() (Intel, MIT; see xegtao_inc.glsl).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "xegtao_inc.glsl"

layout(set = 0, binding = 0) uniform sampler2D source_depth; // Raw depth buffer, internal resolution.

layout(r16f, set = 1, binding = 0) uniform restrict writeonly image2D dest_depth0;
layout(r16f, set = 1, binding = 1) uniform restrict writeonly image2D dest_depth1;
layout(r16f, set = 1, binding = 2) uniform restrict writeonly image2D dest_depth2;
layout(r16f, set = 1, binding = 3) uniform restrict writeonly image2D dest_depth3;
layout(r16f, set = 1, binding = 4) uniform restrict writeonly image2D dest_depth4;

shared float scratch_depths[8][8];

// View depth of AO pixel p_pos. At half resolution an AO pixel takes the
// top-left full resolution pixel of its 2x2 block (no averaging across edges);
// the main pass places it there (ndc_to_view_add).
float load_view_depth(ivec2 p_pos) {
	ivec2 full_pos = clamp(p_pos * params.scale, ivec2(0), params.full_size - 1);
	float raw = texelFetch(source_depth, full_pos, 0).r;
	return xegtao_clamp_depth(xegtao_raw_to_view_depth(raw));
}

// Weighted average depth filter.
float depth_mip_filter(float p_depth0, float p_depth1, float p_depth2, float p_depth3) {
	float max_depth = max(max(p_depth0, p_depth1), max(p_depth2, p_depth3));

	const float depth_range_scale_factor = 0.75; // Found empirically by Intel.
	float effect_radius = depth_range_scale_factor * params.effect_radius;
	float falloff_range = params.effect_falloff_range * effect_radius;
	float falloff_from = effect_radius * (1.0 - params.effect_falloff_range);
	float falloff_mul = -1.0 / falloff_range;
	float falloff_add = falloff_from / falloff_range + 1.0;

	float weight0 = clamp((max_depth - p_depth0) * falloff_mul + falloff_add, 0.0, 1.0);
	float weight1 = clamp((max_depth - p_depth1) * falloff_mul + falloff_add, 0.0, 1.0);
	float weight2 = clamp((max_depth - p_depth2) * falloff_mul + falloff_add, 0.0, 1.0);
	float weight3 = clamp((max_depth - p_depth3) * falloff_mul + falloff_add, 0.0, 1.0);

	float weight_sum = weight0 + weight1 + weight2 + weight3;
	return (weight0 * p_depth0 + weight1 * p_depth1 + weight2 * p_depth2 + weight3 * p_depth3) / weight_sum;
}

void store_depth(int p_mip, ivec2 p_pos, float p_depth) {
	ivec2 mip_size = max(params.size >> p_mip, ivec2(1));
	if (any(greaterThanEqual(p_pos, mip_size))) {
		return;
	}
	switch (p_mip) {
		case 0:
			imageStore(dest_depth0, p_pos, vec4(p_depth));
			break;
		case 1:
			imageStore(dest_depth1, p_pos, vec4(p_depth));
			break;
		case 2:
			imageStore(dest_depth2, p_pos, vec4(p_depth));
			break;
		case 3:
			imageStore(dest_depth3, p_pos, vec4(p_depth));
			break;
		default:
			imageStore(dest_depth4, p_pos, vec4(p_depth));
			break;
	}
}

void main() {
	// MIP 0
	ivec2 base_coord = ivec2(gl_GlobalInvocationID.xy);
	ivec2 group_thread = ivec2(gl_LocalInvocationID.xy);
	ivec2 pix_coord = base_coord * 2;

	float depth0 = load_view_depth(pix_coord + ivec2(0, 0));
	float depth1 = load_view_depth(pix_coord + ivec2(1, 0));
	float depth2 = load_view_depth(pix_coord + ivec2(0, 1));
	float depth3 = load_view_depth(pix_coord + ivec2(1, 1));
	store_depth(0, pix_coord + ivec2(0, 0), depth0);
	store_depth(0, pix_coord + ivec2(1, 0), depth1);
	store_depth(0, pix_coord + ivec2(0, 1), depth2);
	store_depth(0, pix_coord + ivec2(1, 1), depth3);

	// MIP 1
	float dm1 = depth_mip_filter(depth0, depth1, depth2, depth3);
	store_depth(1, base_coord, dm1);
	scratch_depths[group_thread.x][group_thread.y] = dm1;

	barrier();

	// MIP 2
	if (all(equal(group_thread % 2, ivec2(0)))) {
		float in_tl = scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = scratch_depths[group_thread.x + 1][group_thread.y + 0];
		float in_bl = scratch_depths[group_thread.x + 0][group_thread.y + 1];
		float in_br = scratch_depths[group_thread.x + 1][group_thread.y + 1];

		float dm2 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		store_depth(2, base_coord / 2, dm2);
		scratch_depths[group_thread.x][group_thread.y] = dm2;
	}

	barrier();

	// MIP 3
	if (all(equal(group_thread % 4, ivec2(0)))) {
		float in_tl = scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = scratch_depths[group_thread.x + 2][group_thread.y + 0];
		float in_bl = scratch_depths[group_thread.x + 0][group_thread.y + 2];
		float in_br = scratch_depths[group_thread.x + 2][group_thread.y + 2];

		float dm3 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		store_depth(3, base_coord / 4, dm3);
		scratch_depths[group_thread.x][group_thread.y] = dm3;
	}

	barrier();

	// MIP 4
	if (all(equal(group_thread % 8, ivec2(0)))) {
		float in_tl = scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = scratch_depths[group_thread.x + 4][group_thread.y + 0];
		float in_bl = scratch_depths[group_thread.x + 0][group_thread.y + 4];
		float in_br = scratch_depths[group_thread.x + 4][group_thread.y + 4];

		float dm4 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		store_depth(4, base_coord / 8, dm4);
	}
}
