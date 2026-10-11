#[compute]

#version 450

#VERSION_DEFINES

// XeGTAO pass 2: horizon search ("Algorithm 1" of Jimenez et al. 2016) with
// slices and steps per slice set by the quality (SLICE_COUNT, STEPS_PER_SLICE),
// the depth based edges for the denoiser and, with BENT_NORMALS, the bent
// normal ("Algorithm 2").
// Based on XeGTAO_MainPass() (Intel, MIT; see xegtao_inc.glsl).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "xegtao_inc.glsl"

layout(set = 0, binding = 0) uniform sampler2D source_depth; // View depth with 5 MIPs (point sampled).
layout(set = 0, binding = 1) uniform sampler2D source_normal_roughness; // Internal resolution.

#ifdef BENT_NORMALS
layout(r32ui, set = 1, binding = 0) uniform restrict writeonly uimage2D dest_ao;
#else
layout(r8, set = 1, binding = 0) uniform restrict writeonly image2D dest_ao;
#endif
layout(r8, set = 1, binding = 1) uniform restrict writeonly image2D dest_edges;

#define XE_HILBERT_LEVEL 6U
#define XE_HILBERT_WIDTH (1U << XE_HILBERT_LEVEL)

// From https://www.shadertoy.com/view/3tB3z3 (via XeGTAO).
uint hilbert_index(uint p_pos_x, uint p_pos_y) {
	uint index = 0U;
	for (uint cur_level = XE_HILBERT_WIDTH / 2U; cur_level > 0U; cur_level /= 2U) {
		uint region_x = (p_pos_x & cur_level) > 0U ? 1U : 0U;
		uint region_y = (p_pos_y & cur_level) > 0U ? 1U : 0U;
		index += cur_level * cur_level * ((3U * region_x) ^ region_y);
		if (region_y == 0U) {
			if (region_x == 1U) {
				p_pos_x = (XE_HILBERT_WIDTH - 1U) - p_pos_x;
				p_pos_y = (XE_HILBERT_WIDTH - 1U) - p_pos_y;
			}
			uint temp = p_pos_x;
			p_pos_x = p_pos_y;
			p_pos_y = temp;
		}
	}
	return index;
}

// Hilbert curve driving the R2 sequence; the index advances per frame only
// with temporal anti-aliasing (noise_index is 0 otherwise).
vec2 spatio_temporal_noise(uvec2 p_pix_coord, uint p_temporal_index) {
	uint index = hilbert_index(p_pix_coord.x % XE_HILBERT_WIDTH, p_pix_coord.y % XE_HILBERT_WIDTH);
	index += 288U * (p_temporal_index % 64U); // 288: the best offset found by Intel for level 6.
	return fract(0.5 + float(index) * vec2(0.75487766624669276005, 0.5698402909980532659114));
}

vec3 load_normal(ivec2 p_pos) {
	ivec2 full_pos = clamp(p_pos * params.scale, ivec2(0), params.full_size - 1);
	vec3 normal = normalize(texelFetch(source_normal_roughness, full_pos, 0).xyz * 2.0 - 1.0);
	normal.z = -normal.z; // Godot view space (z backward) to XeGTAO's (z forward).
	return normal;
}

float load_depth(ivec2 p_pos) {
	return texelFetch(source_depth, clamp(p_pos, ivec2(0), params.size - 1), 0).r;
}

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN.
float fast_sqrt(float p_x) {
	return intBitsToFloat(0x1fbd1df5 + (floatBitsToInt(p_x) >> 1));
}

// Input [-1, 1], output [0, PI]; from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float fast_acos(float p_in_x) {
	const float PI = 3.141593;
	const float HALF_PI = 1.570796;
	float x = abs(p_in_x);
	float res = -0.156583 * x + HALF_PI;
	res *= fast_sqrt(1.0 - x);
	return (p_in_x >= 0.0) ? res : PI - res;
}

// Rotation taking p_from to p_to (Moller and Hughes 1999); only used with
// p_from = (0, 0, -1).
mat3 rot_from_to_matrix(vec3 p_from, vec3 p_to) {
	float e = dot(p_from, p_to);
	float f = abs(e);
	if (f > 1.0 - 0.0003) {
		return mat3(1.0);
	}
	vec3 v = cross(p_from, p_to);
	float h = 1.0 / (1.0 + e);
	float hvx = h * v.x;
	float hvz = h * v.z;
	float hvxy = hvx * v.y;
	float hvxz = hvx * v.z;
	float hvyz = hvz * v.y;

	// Rows as in the HLSL reference; GLSL matrices are built from columns.
	mat3 m;
	m[0][0] = e + hvx * v.x;
	m[1][0] = hvxy - v.z;
	m[2][0] = hvxz + v.y;

	m[0][1] = hvxy + v.z;
	m[1][1] = e + h * v.y * v.y;
	m[2][1] = hvyz - v.x;

	m[0][2] = hvxz - v.y;
	m[1][2] = hvyz + v.x;
	m[2][2] = e + hvz * v.z;
	return m;
}

void output_working_term(ivec2 p_pix_coord, float p_visibility, vec3 p_bent_normal) {
	p_visibility = clamp(p_visibility / XE_GTAO_OCCLUSION_TERM_SCALE, 0.0, 1.0);
#ifdef BENT_NORMALS
	imageStore(dest_ao, p_pix_coord, uvec4(xegtao_encode_visibility_bent_normal(p_visibility, p_bent_normal)));
#else
	imageStore(dest_ao, p_pix_coord, vec4(p_visibility));
#endif
}

void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.size))) {
		return;
	}

	vec2 normalized_screen_pos = (vec2(pix_coord) + 0.5) * params.pixel_size;

	float viewspace_z = load_depth(pix_coord);
	float pix_lz = load_depth(pix_coord + ivec2(-1, 0));
	float pix_tz = load_depth(pix_coord + ivec2(0, -1));
	float pix_rz = load_depth(pix_coord + ivec2(1, 0));
	float pix_bz = load_depth(pix_coord + ivec2(0, 1));

	vec4 edges_lrtb = xegtao_calculate_edges(viewspace_z, pix_lz, pix_rz, pix_tz, pix_bz);
	imageStore(dest_edges, pix_coord, vec4(xegtao_pack_edges(edges_lrtb)));

	vec3 viewspace_normal = load_normal(pix_coord);

	// Background (sky): nothing to occlude.
	if (viewspace_z >= params.sky_depth) {
		output_working_term(pix_coord, 1.0, viewspace_normal);
		return;
	}

	// Move the center slightly towards the camera against FP16 depth imprecision.
	viewspace_z *= 0.99920;

	vec3 pix_center_pos = xegtao_view_position(normalized_screen_pos, viewspace_z);
	vec3 view_vec = params.orthogonal != 0 ? vec3(0.0, 0.0, -1.0) : normalize(-pix_center_pos);

	float effect_radius = params.effect_radius;
	float sample_distribution_power = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
	float falloff_range = params.effect_falloff_range * effect_radius;
	float falloff_from = effect_radius * (1.0 - params.effect_falloff_range);

	// Fade out precompute.
	float falloff_mul = -1.0 / falloff_range;
	float falloff_add = falloff_from / falloff_range + 1.0;

	float visibility = 0.0;
#ifdef BENT_NORMALS
	vec3 bent_normal = vec3(0.0);
#else
	vec3 bent_normal = viewspace_normal;
#endif

	vec2 local_noise = spatio_temporal_noise(uvec2(pix_coord), params.noise_index);
	float noise_slice = local_noise.x;
	float noise_sample = local_noise.y;

	// If the offset is under about a pixel, push it out to the minimum distance.
	const float pixel_too_close_threshold = 1.3;

	// View space size of a pixel at the center depth.
	vec2 pixel_dir_rb_viewspace_size_at_center_z = params.orthogonal != 0 ? params.ndc_to_view_mul_x_pixel_size : viewspace_z * params.ndc_to_view_mul_x_pixel_size;

	float screenspace_radius = effect_radius / pixel_dir_rb_viewspace_size_at_center_z.x;

	// Fade out for small screen radii.
	visibility += clamp((10.0 - screenspace_radius) / 100.0, 0.0, 1.0) * 0.5;

	// Minimum distance to sample from, to avoid sampling the center pixel.
	float min_s = pixel_too_close_threshold / screenspace_radius;

	for (int slice = 0; slice < SLICE_COUNT; slice++) {
		float slice_k = (float(slice) + noise_slice) / float(SLICE_COUNT);
		// Lines 5 and 6 of the paper.
		float phi = slice_k * XE_GTAO_PI;
		float cos_phi = cos(phi);
		float sin_phi = sin(phi);
		vec2 omega = vec2(cos_phi, -sin_phi); // Screen space: y down.

		// Convert to screen units (pixels).
		omega *= screenspace_radius;

		// Line 8.
		vec3 direction_vec = vec3(cos_phi, sin_phi, 0.0);

		// Line 9.
		vec3 ortho_direction_vec = direction_vec - (dot(direction_vec, view_vec) * view_vec);

		// Line 10: orthogonal to direction_vec and view_vec, defines the projected normal.
		vec3 axis_vec = normalize(cross(ortho_direction_vec, view_vec));

		// Line 11.
		vec3 projected_normal_vec = viewspace_normal - axis_vec * dot(viewspace_normal, axis_vec);

		// Line 13.
		float sign_norm = sign(dot(ortho_direction_vec, projected_normal_vec));

		// Line 14.
		float projected_normal_vec_length = length(projected_normal_vec);
		float cos_norm = clamp(dot(projected_normal_vec, view_vec) / max(projected_normal_vec_length, 1e-6), 0.0, 1.0);

		// Line 15.
		float n = sign_norm * fast_acos(cos_norm);

		// Lower weight target; not -1 as in the paper, because it is under the horizon.
		float low_horizon_cos0 = cos(n + XE_GTAO_PI_HALF);
		float low_horizon_cos1 = cos(n - XE_GTAO_PI_HALF);

		// Lines 17 and 18, the "side" loop unrolled.
		float horizon_cos0 = low_horizon_cos0;
		float horizon_cos1 = low_horizon_cos1;

		for (int step_i = 0; step_i < STEPS_PER_SLICE; step_i++) {
			// R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/).
			float step_base_noise = float(slice + step_i * STEPS_PER_SLICE) * 0.6180339887498948482;
			float step_noise = fract(noise_sample + step_base_noise);

			// Approximately line 20, with noise.
			float s = (float(step_i) + step_noise) / float(STEPS_PER_SLICE);

			// Small crevices matter more than big surfaces.
			s = pow(s, sample_distribution_power);

			// Avoid sampling the center pixel.
			s += min_s;

			// Approximately lines 21 and 22.
			vec2 sample_offset = s * omega;

			float sample_offset_length = length(sample_offset);

			// Point sampled MIP: linear filtering would mix neighboring depths.
			float mip_level = clamp(log2(sample_offset_length) - XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET, 0.0, float(XE_GTAO_DEPTH_MIP_LEVELS - 1));
			mip_level = floor(mip_level + 0.5);

			// Snap to the pixel center: more correct direction math.
			sample_offset = round(sample_offset) * params.pixel_size;

			vec2 sample_screen_pos0 = normalized_screen_pos + sample_offset;
			float sz0 = textureLod(source_depth, sample_screen_pos0, mip_level).x;
			vec3 sample_pos0 = xegtao_view_position(sample_screen_pos0, sz0);

			vec2 sample_screen_pos1 = normalized_screen_pos - sample_offset;
			float sz1 = textureLod(source_depth, sample_screen_pos1, mip_level).x;
			vec3 sample_pos1 = xegtao_view_position(sample_screen_pos1, sz1);

			vec3 sample_delta0 = sample_pos0 - pix_center_pos;
			vec3 sample_delta1 = sample_pos1 - pix_center_pos;
			float sample_dist0 = length(sample_delta0);
			float sample_dist1 = length(sample_delta1);

			// Approximately lines 23 and 24.
			vec3 sample_horizon_vec0 = sample_delta0 / max(sample_dist0, 1e-6);
			vec3 sample_horizon_vec1 = sample_delta1 / max(sample_dist1, 1e-6);

			// Samples beyond the radius fade out over the falloff range.
			float weight0 = clamp(sample_dist0 * falloff_mul + falloff_add, 0.0, 1.0);
			float weight1 = clamp(sample_dist1 * falloff_mul + falloff_add, 0.0, 1.0);

			// Sample horizon cosines.
			float shc0 = dot(sample_horizon_vec0, view_vec);
			float shc1 = dot(sample_horizon_vec1, view_vec);

			// Discard unwanted samples.
			shc0 = mix(low_horizon_cos0, shc0, weight0);
			shc1 = mix(low_horizon_cos1, shc1, weight1);

			horizon_cos0 = max(horizon_cos0, shc0);
			horizon_cos1 = max(horizon_cos1, shc1);
		}

		// Counters slight overdarkening on high slopes (Intel's fudge).
		projected_normal_vec_length = mix(projected_normal_vec_length, 1.0, 0.05);

		// Line ~27.
		float h0 = -fast_acos(horizon_cos1);
		float h1 = fast_acos(horizon_cos0);

		float iarc0 = (cos_norm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
		float iarc1 = (cos_norm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
		float local_visibility = projected_normal_vec_length * (iarc0 + iarc1);
		visibility += local_visibility;

#ifdef BENT_NORMALS
		// "Algorithm 2: extension that computes bent normals".
		float t0 = (6.0 * sin(h0 - n) - sin(3.0 * h0 - n) + 6.0 * sin(h1 - n) - sin(3.0 * h1 - n) + 16.0 * sin(n) - 3.0 * (sin(h0 + n) + sin(h1 + n))) / 12.0;
		float t1 = (-cos(3.0 * h0 - n) - cos(3.0 * h1 - n) + 8.0 * cos(n) - 3.0 * (cos(h0 + n) + cos(h1 + n))) / 12.0;
		vec3 local_bent_normal = vec3(direction_vec.x * t0, direction_vec.y * t0, -t1);
		local_bent_normal = rot_from_to_matrix(vec3(0.0, 0.0, -1.0), view_vec) * local_bent_normal * projected_normal_vec_length;
		bent_normal += local_bent_normal;
#endif
	}
	visibility /= float(SLICE_COUNT);
	visibility = pow(max(visibility, 0.0), params.final_value_power);
	// No total occlusion: the pixel is visible; also helps bent normal packing.
	visibility = max(0.03, visibility);

#ifdef BENT_NORMALS
	bent_normal = normalize(bent_normal);
#endif

	output_working_term(pix_coord, visibility, bent_normal);
}
