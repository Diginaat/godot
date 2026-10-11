// XeGTAO (ground truth ambient occlusion), ported to Godot's GLSL / RenderingDevice.
//
// Based on Intel's XeGTAO 1.30 (https://github.com/GameTechDev/XeGTAO),
// Copyright (C) 2016-2021, Intel Corporation, SPDX-License-Identifier: MIT,
// which implements GTAO from "Jimenez et al., Practical Real-Time Strategies
// for Accurate Indirect Occlusion" (2016). See thirdparty/README.md for the
// license notice.
//
// View space here is XeGTAO's: x right, y up, z forward (positive depth).
// Godot's view space has z backward, so normals read from the normal-roughness
// buffer and bent normals written back get z negated.

#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

#define XE_GTAO_DEPTH_MIP_LEVELS 5
// Raw (pre-denoise) visibility can overshoot 1 and averages out later; it is
// stored divided by this to fit a UNORM format.
#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5

// Auto-tuned constants from the reference implementation (XeGTAO.h).
#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER 1.457
#define XE_GTAO_DEFAULT_FALLOFF_RANGE 0.615
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER 2.0
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET 3.30

layout(push_constant, std430) uniform Params {
	ivec2 size; // AO resolution (internal resolution, or half of it).
	vec2 pixel_size; // 1 / size.

	vec2 ndc_to_view_mul; // View xy at depth 1 (perspective) or absolute (orthographic) from screen uv.
	vec2 ndc_to_view_add;

	vec2 ndc_to_view_mul_x_pixel_size;
	float effect_radius; // Radius x XE_GTAO_DEFAULT_RADIUS_MULTIPLIER.
	float effect_falloff_range;

	float final_value_power;
	float denoise_blur_beta;
	uint noise_index; // Frame index % 64 with temporal AA, else 0 (static noise).
	uint orthogonal;

	vec4 depth_unproject; // Raw depth d to view depth: -(x * d + y) / (z * d + w).

	ivec2 full_size; // Internal resolution (depth and normal buffers).
	int scale; // full / AO resolution: 1 or 2.
	float sky_depth; // View depth at and beyond which a pixel is background.

	uint debug_mode;
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

// Depth buffer value (Vulkan, reverse Z) to positive view space depth.
float xegtao_raw_to_view_depth(float p_raw) {
	vec4 u = params.depth_unproject;
	return -(u.x * p_raw + u.y) / (u.z * p_raw + u.w);
}

// Clamped to the FP16 range of the depth MIP chain.
float xegtao_clamp_depth(float p_depth) {
	return clamp(p_depth, 0.0, 65504.0);
}

vec3 xegtao_view_position(vec2 p_screen_pos, float p_view_depth) {
	vec3 ret;
	if (params.orthogonal != 0) {
		ret.xy = params.ndc_to_view_mul * p_screen_pos + params.ndc_to_view_add;
	} else {
		ret.xy = (params.ndc_to_view_mul * p_screen_pos + params.ndc_to_view_add) * p_view_depth;
	}
	ret.z = p_view_depth;
	return ret;
}

vec4 xegtao_calculate_edges(float p_center_z, float p_left_z, float p_right_z, float p_top_z, float p_bottom_z) {
	vec4 edges_lrtb = vec4(p_left_z, p_right_z, p_top_z, p_bottom_z) - p_center_z;

	float slope_lr = (edges_lrtb.y - edges_lrtb.x) * 0.5;
	float slope_tb = (edges_lrtb.w - edges_lrtb.z) * 0.5;
	vec4 edges_lrtb_slope_adjusted = edges_lrtb + vec4(slope_lr, -slope_lr, slope_tb, -slope_tb);
	edges_lrtb = min(abs(edges_lrtb), abs(edges_lrtb_slope_adjusted));
	return clamp(1.25 - edges_lrtb / (p_center_z * 0.011), 0.0, 1.0);
}

// 2 bits per edge: 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions.
float xegtao_pack_edges(vec4 p_edges_lrtb) {
	p_edges_lrtb = round(clamp(p_edges_lrtb, 0.0, 1.0) * 2.9);
	return dot(p_edges_lrtb, vec4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

vec4 xegtao_unpack_edges(float p_packed) {
	uint packed_val = uint(p_packed * 255.5);
	vec4 edges_lrtb;
	edges_lrtb.x = float((packed_val >> 6) & 0x03) / 3.0;
	edges_lrtb.y = float((packed_val >> 4) & 0x03) / 3.0;
	edges_lrtb.z = float((packed_val >> 2) & 0x03) / 3.0;
	edges_lrtb.w = float((packed_val >> 0) & 0x03) / 3.0;
	return clamp(edges_lrtb, 0.0, 1.0);
}

vec4 xegtao_r8g8b8a8_unorm_to_float4(uint p_packed) {
	return vec4(float(p_packed & 0xff), float((p_packed >> 8) & 0xff), float((p_packed >> 16) & 0xff), float(p_packed >> 24)) / 255.0;
}

uint xegtao_float4_to_r8g8b8a8_unorm(vec4 p_value) {
	uvec4 v = uvec4(clamp(p_value, 0.0, 1.0) * 255.0 + 0.5);
	return v.x | (v.y << 8) | (v.z << 16) | (v.w << 24);
}

uint xegtao_encode_visibility_bent_normal(float p_visibility, vec3 p_bent_normal) {
	return xegtao_float4_to_r8g8b8a8_unorm(vec4(p_bent_normal * 0.5 + 0.5, p_visibility));
}

void xegtao_decode_visibility_bent_normal(uint p_packed, out float r_visibility, out vec3 r_bent_normal) {
	vec4 decoded = xegtao_r8g8b8a8_unorm_to_float4(p_packed);
	r_bent_normal = decoded.xyz * 2.0 - 1.0; // Normalized only at the end.
	r_visibility = decoded.w;
}
