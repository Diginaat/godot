#[compute]

#version 450

#VERSION_DEFINES

// XeGTAO pass 3: edge-aware spatial denoise (3x3, weights from the packed
// depth edges of the main pass). Run once with temporal AA, twice without;
// the last pass (FINAL_PASS) rescales the visibility and writes the outputs
// the renderer reads: visibility (R8) and, with BENT_NORMALS, the bent normal
// in Godot's view space (RGBA8: xyz * 0.5 + 0.5, visibility in alpha).
// Based on XeGTAO_Denoise() (Intel, MIT; see xegtao_inc.glsl); one pixel per
// thread with clamped fetches instead of the reference's gathers.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "xegtao_inc.glsl"

#ifdef BENT_NORMALS
layout(set = 0, binding = 0) uniform usampler2D source_ao;
#else
layout(set = 0, binding = 0) uniform sampler2D source_ao;
#endif
layout(set = 0, binding = 1) uniform sampler2D source_edges;

#ifdef FINAL_PASS
layout(r8, set = 1, binding = 0) uniform restrict writeonly image2D dest_visibility;
#ifdef BENT_NORMALS
layout(rgba8, set = 1, binding = 1) uniform restrict writeonly image2D dest_bent_normal;
#endif
#else
#ifdef BENT_NORMALS
layout(r32ui, set = 1, binding = 0) uniform restrict writeonly uimage2D dest_ao;
#else
layout(r8, set = 1, binding = 0) uniform restrict writeonly image2D dest_ao;
#endif
#endif

#ifdef BENT_NORMALS
#define AOTermType vec4 // xyz bent normal, w visibility.
#else
#define AOTermType float
#endif

ivec2 clamp_coord(ivec2 p_pos) {
	return clamp(p_pos, ivec2(0), params.size - 1);
}

AOTermType load_ao(ivec2 p_pos) {
#ifdef BENT_NORMALS
	vec4 value;
	xegtao_decode_visibility_bent_normal(texelFetch(source_ao, clamp_coord(p_pos), 0).r, value.w, value.xyz);
	return value;
#else
	return texelFetch(source_ao, clamp_coord(p_pos), 0).r;
#endif
}

vec4 load_edges(ivec2 p_pos) {
	return xegtao_unpack_edges(texelFetch(source_edges, clamp_coord(p_pos), 0).r);
}

void add_sample(AOTermType p_value, float p_weight, inout AOTermType r_sum, inout float r_sum_weight) {
	r_sum += p_weight * p_value;
	r_sum_weight += p_weight;
}

void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.size))) {
		return;
	}

#ifdef FINAL_PASS
	const float blur_amount = params.denoise_blur_beta;
#else
	const float blur_amount = params.denoise_blur_beta / 5.0;
#endif
	const float diag_weight = 0.85 * 0.5;

	vec4 edges_l_lrtb = load_edges(pix_coord + ivec2(-1, 0));
	vec4 edges_t_lrtb = load_edges(pix_coord + ivec2(0, -1));
	vec4 edges_r_lrtb = load_edges(pix_coord + ivec2(1, 0));
	vec4 edges_b_lrtb = load_edges(pix_coord + ivec2(0, 1));
	vec4 edges_c_lrtb = load_edges(pix_coord);

	// Edges aren't perfectly symmetrical: enforce it, for a slightly sharper blur.
	edges_c_lrtb *= vec4(edges_l_lrtb.y, edges_r_lrtb.x, edges_t_lrtb.w, edges_b_lrtb.z);

	// Allow a little AO to leak from neighbors with 3 or 4 edges: less spatial
	// and temporal aliasing.
	const float leak_threshold = 2.5;
	const float leak_strength = 0.5;
	float edginess = (clamp(4.0 - leak_threshold - dot(edges_c_lrtb, vec4(1.0)), 0.0, 1.0) / (4.0 - leak_threshold)) * leak_strength;
	edges_c_lrtb = clamp(edges_c_lrtb + edginess, 0.0, 1.0);

	// Diagonals.
	float weight_tl = diag_weight * (edges_c_lrtb.x * edges_l_lrtb.z + edges_c_lrtb.z * edges_t_lrtb.x);
	float weight_tr = diag_weight * (edges_c_lrtb.z * edges_t_lrtb.y + edges_c_lrtb.y * edges_r_lrtb.z);
	float weight_bl = diag_weight * (edges_c_lrtb.w * edges_b_lrtb.x + edges_c_lrtb.x * edges_l_lrtb.w);
	float weight_br = diag_weight * (edges_c_lrtb.y * edges_r_lrtb.w + edges_c_lrtb.w * edges_b_lrtb.y);

	float sum_weight = blur_amount;
	AOTermType sum = load_ao(pix_coord) * sum_weight;

	add_sample(load_ao(pix_coord + ivec2(-1, 0)), edges_c_lrtb.x, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(1, 0)), edges_c_lrtb.y, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(0, -1)), edges_c_lrtb.z, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(0, 1)), edges_c_lrtb.w, sum, sum_weight);

	add_sample(load_ao(pix_coord + ivec2(-1, -1)), weight_tl, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(1, -1)), weight_tr, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(-1, 1)), weight_bl, sum, sum_weight);
	add_sample(load_ao(pix_coord + ivec2(1, 1)), weight_br, sum, sum_weight);

	AOTermType ao_term = sum / sum_weight;

#ifdef FINAL_PASS
#ifdef BENT_NORMALS
	float visibility = clamp(ao_term.w * XE_GTAO_OCCLUSION_TERM_SCALE, 0.0, 1.0);
	vec3 bent_normal = normalize(ao_term.xyz);
	bent_normal.z = -bent_normal.z; // Back to Godot's view space.
	imageStore(dest_visibility, pix_coord, vec4(visibility));
	imageStore(dest_bent_normal, pix_coord, vec4(bent_normal * 0.5 + 0.5, visibility));
#else
	imageStore(dest_visibility, pix_coord, vec4(clamp(ao_term * XE_GTAO_OCCLUSION_TERM_SCALE, 0.0, 1.0)));
#endif
#else
#ifdef BENT_NORMALS
	imageStore(dest_ao, pix_coord, uvec4(xegtao_encode_visibility_bent_normal(ao_term.w, normalize(ao_term.xyz))));
#else
	imageStore(dest_ao, pix_coord, vec4(ao_term));
#endif
#endif
}
