#[compute]

#version 450

#VERSION_DEFINES

// XeGTAO debug views (DDGIVolume.ao_debug_view), drawn over the frame at the
// internal resolution: 1 raw AO (before the denoiser), 2 final AO, 3 view
// depth, 4 normals, 5 bent normals, 6 the denoiser's edges (white = no edge).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "xegtao_inc.glsl"

#ifdef BENT_NORMALS
layout(set = 0, binding = 0) uniform usampler2D source_raw_ao; // AO resolution.
#else
layout(set = 0, binding = 0) uniform sampler2D source_raw_ao;
#endif
layout(set = 0, binding = 1) uniform sampler2D source_edges; // AO resolution.
layout(set = 0, binding = 2) uniform sampler2D source_view_depth; // AO resolution.
layout(set = 0, binding = 3) uniform sampler2D source_normal_roughness; // Internal resolution.
layout(set = 0, binding = 4) uniform sampler2D source_visibility; // Internal resolution.
layout(set = 0, binding = 5) uniform sampler2D source_bent_normal; // Internal resolution (black without bent normals).

layout(rgba8, set = 1, binding = 0) uniform restrict writeonly image2D dest_debug;

void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.full_size))) {
		return;
	}
	ivec2 ao_coord = clamp(pix_coord / params.scale, ivec2(0), params.size - 1);

	vec3 color = vec3(0.0);
	switch (params.debug_mode) {
		case 1: { // Raw AO.
#ifdef BENT_NORMALS
			float v;
			vec3 n;
			xegtao_decode_visibility_bent_normal(texelFetch(source_raw_ao, ao_coord, 0).r, v, n);
#else
			float v = texelFetch(source_raw_ao, ao_coord, 0).r;
#endif
			color = vec3(clamp(v * XE_GTAO_OCCLUSION_TERM_SCALE, 0.0, 1.0));
		} break;
		case 2: { // Final AO.
			color = vec3(texelFetch(source_visibility, pix_coord, 0).r);
		} break;
		case 3: { // View depth, in units of the effect radius (repeating bands).
			float z = texelFetch(source_view_depth, ao_coord, 0).r;
			color = z >= params.sky_depth ? vec3(0.0, 0.0, 0.3) : vec3(fract(z / max(params.effect_radius * 4.0, 1e-3)), 1.0 / (1.0 + z * 0.1), 0.0);
		} break;
		case 4: { // Normals (Godot view space).
			vec3 n = normalize(texelFetch(source_normal_roughness, pix_coord, 0).xyz * 2.0 - 1.0);
			color = n * 0.5 + 0.5;
		} break;
		case 5: { // Bent normals (Godot view space).
			color = texelFetch(source_bent_normal, pix_coord, 0).rgb;
		} break;
		case 6: { // Edges.
			vec4 e = xegtao_unpack_edges(texelFetch(source_edges, ao_coord, 0).r);
			color = vec3(e.x, e.y * 0.5 + e.w * 0.5, e.z);
		} break;
	}
	imageStore(dest_debug, pix_coord, vec4(color, 1.0));
}
