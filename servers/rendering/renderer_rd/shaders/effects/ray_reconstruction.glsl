#[compute]

#version 450

#VERSION_DEFINES

// Native ray reconstruction (path tracer denoiser), an independent
// implementation of spatiotemporal variance-guided filtering (Schied et al.
// 2017) with temporal gradients for adaptive history (A-SVGF, Schied, Peters,
// Dachsbacher 2018; reference code BSD-3-Clause, Copyright (c) 2018 Christoph
// Schied, see COPYRIGHT.txt). See docs/renderer/native_ray_reconstruction.md.
//
// Passes (one shader, one mode each):
//   MODE_GRADIENT_CLEAR    empties the gradient sample tiles (new buffers)
//   MODE_FORWARD_PROJECT   before the trace: one random pixel per 3x3 tile of
//                          the last frame is projected into this frame; the
//                          path tracer replays it there (same random numbers)
//   MODE_GRADIENT          after the trace: replayed result minus last frame's
//                          result, per tile
//   MODE_GRADIENT_FILTER   a-trous over the tile gradients
//   MODE_TEMPORAL  demodulate, reproject, validate and accumulate history
//   MODE_VARIANCE  per-pixel variance (spatial estimate for short history)
//   MODE_ATROUS    one edge-aware a-trous iteration; the last one composes
//                  the final image
//   MODE_REFERENCE debug: plain running average of the path tracer's image
//                  while nothing moves (a converged reference for testing)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "../oct_inc.glsl"

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // Current, jittered (matches the path tracer's primary rays).
	mat4 view_to_world_rotation; // Current camera rotation (no translation).
	mat4 current_to_previous_view; // Current view space -> previous view space.
	vec4 size; // width, height, 1/width, 1/height
	vec4 history; // max diffuse history, max specular history, max moment history, history valid (0/1)
	vec4 filter_params; // luminance sigma, normal power, depth sigma, pixel footprint at depth 1
	vec4 view_ray; // View ray at unit depth: xy = scale * uv + offset (scale.xy, offset.xy).
	mat4 projection_unjittered; // Current view space -> clip, without jitter.
	mat4 previous_projection_unjittered; // Previous view space -> clip, without jitter.
	mat4 previous_to_current_view; // Previous view space -> current view space.
	vec4 previous_view_ray; // As view_ray, for the previous frame.
	vec4 specular_params; // x: virtual reprojection below this roughness (blends out above), yzw unused.
	vec4 reserved[2];
}
params;

layout(push_constant, std430) uniform PushConstant {
	int step_size; // A-trous step in pixels.
	int iteration;
	uint flags; // PC_FLAG_*
	uint debug_mode;
}
pc;

#define PC_FLAG_COMPOSE 2u // Last iteration: write the final image.
#define PC_FLAG_RESET 4u // Reference mode: start a new average.

#define DEBUG_NONE 0u
#define DEBUG_CLEAN 1u
#define DEBUG_DIFFUSE 2u
#define DEBUG_SPECULAR 3u
#define DEBUG_RAW 4u
#define DEBUG_HISTORY_LENGTH 5u
#define DEBUG_VARIANCE 6u
#define DEBUG_SPLIT 7u
#define DEBUG_REFERENCE 8u

// Shared helpers ------------------------------------------------------------

const float ALBEDO_MIN = 1.0 / 255.0;

float luminance(vec3 c) {
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 view_position(vec2 uv, float ndc_depth) {
	vec4 p = params.inv_projection * vec4(uv * 2.0 - 1.0, ndc_depth, 1.0);
	return p.xyz / p.w;
}

// Surface record kept for the next frame and for the a-trous weights:
// x = linear depth (float bits, 0 = sky), y = normal (2 x 12 bits octahedral) | roughness (8 bits).
uvec2 pack_surface(float linear_depth, vec3 normal, float roughness) {
	vec2 o = clamp(vec3_to_oct(normal), 0.0, 1.0);
	uvec2 q = uvec2(o * 4095.0 + 0.5);
	uint r = uint(clamp(roughness, 0.0, 1.0) * 255.0 + 0.5);
	return uvec2(floatBitsToUint(linear_depth), q.x | (q.y << 12u) | (r << 24u));
}

void unpack_surface(uvec2 s, out float linear_depth, out vec3 normal, out float roughness) {
	linear_depth = uintBitsToFloat(s.x);
	vec2 o = vec2(float(s.y & 0xFFFu), float((s.y >> 12u) & 0xFFFu)) / 4095.0;
	normal = oct_to_vec3(o * 2.0 - 1.0);
	roughness = float(s.y >> 24u) / 255.0;
}

vec3 guide_normal(uvec4 g) {
	return oct_to_vec3(unpackUnorm2x16(g.z) * 2.0 - 1.0);
}

float guide_roughness(uvec4 g) {
	return unpackHalf2x16(g.w).x;
}

// Distance to what the perfect reflection hits; < 0 when not traced.
float guide_mirror_hit(uvec4 g) {
	return unpackHalf2x16(g.w).y;
}

vec2 project_uv(mat4 p_projection, vec3 p_view) {
	vec4 clip = p_projection * vec4(p_view, 1.0);
	return clip.xy / clip.w * 0.5 + 0.5;
}

vec3 guide_diffuse_albedo(uvec4 g) {
	return max(unpackUnorm4x8(g.x).rgb, vec3(ALBEDO_MIN));
}

vec3 guide_specular_albedo(uvec4 g) {
	return max(unpackUnorm4x8(g.y).rgb, vec3(ALBEDO_MIN));
}

// Glass and other refractive surfaces: the specular signal is the light seen
// through them.
bool guide_transmissive(uvec4 g) {
	return (g.y >> 24u) != 0u;
}

vec3 sanitize(vec3 c) {
	return any(isnan(c)) || any(isinf(c)) ? vec3(0.0) : max(c, vec3(0.0));
}

#ifdef MODE_TEMPORAL

layout(rgba16f, set = 1, binding = 0) uniform restrict readonly image2D diffuse_image;
layout(rgba16f, set = 1, binding = 1) uniform restrict readonly image2D specular_image;
layout(rgba32ui, set = 1, binding = 2) uniform restrict readonly uimage2D guide_image;
layout(r32f, set = 1, binding = 3) uniform restrict readonly image2D depth_image;
layout(rg16f, set = 1, binding = 4) uniform restrict readonly image2D velocity_image;

// Sampled (linear, clamped) for the Catmull-Rom resampling of the colors.
layout(set = 1, binding = 5) uniform sampler2D prev_diffuse_history;
layout(set = 1, binding = 6) uniform sampler2D prev_specular_history;
layout(rgba16f, set = 1, binding = 7) uniform restrict readonly image2D prev_moments_history;
layout(rg32ui, set = 1, binding = 8) uniform restrict readonly uimage2D prev_surface;

layout(rgba16f, set = 1, binding = 9) uniform restrict writeonly image2D diffuse_history;
layout(rgba16f, set = 1, binding = 10) uniform restrict writeonly image2D specular_history;
layout(rgba16f, set = 1, binding = 11) uniform restrict writeonly image2D moments_history;
layout(rg32ui, set = 1, binding = 12) uniform restrict writeonly uimage2D surface;
// Specular hit distance (0 = unknown), accumulated like the colors.
layout(r16f, set = 1, binding = 13) uniform restrict readonly image2D prev_specular_hit_history;
layout(r16f, set = 1, binding = 14) uniform restrict writeonly image2D specular_hit_history;
// Filtered temporal gradients per 3x3 tile: (diffuse change, diffuse max,
// specular change, specular max); max < 0 where no gradient is known.
layout(rgba16f, set = 1, binding = 15) uniform restrict readonly image2D gradient_image;
// This frame's raw luminance (diffuse, specular), for the next frame's gradients.
layout(rg16f, set = 1, binding = 16) uniform restrict writeonly image2D raw_luminance;

// Last frame's specular history at the place where the reflection seen at this
// pixel was: the virtual image of the hit point, p_hit_distance behind the
// surface along the view ray. Taps must lie on the same surface (plane and
// normal test in the current view). Returns the weight found.
float reproject_virtual(vec3 p_view_pos, vec3 p_view_normal, vec3 p_normal, float p_hit_distance, vec2 p_uv, ivec2 p_size, out vec4 r_specular) {
	r_specular = vec4(0.0);
	vec3 virtual_pos = p_view_pos * (1.0 + p_hit_distance / max(length(p_view_pos), 1e-4));
	vec3 previous_virtual = (params.current_to_previous_view * vec4(virtual_pos, 1.0)).xyz;
	vec2 delta = project_uv(params.previous_projection_unjittered, previous_virtual) - project_uv(params.projection_unjittered, virtual_pos);
	vec2 prev_pixel = (p_uv + delta) * params.size.xy - 0.5;
	ivec2 base = ivec2(floor(prev_pixel));
	vec2 f = prev_pixel - vec2(base);
	float depth = -p_view_pos.z;
	float plane_tolerance = 0.01 * depth + 2.0 * params.filter_params.w * depth;
	float weight_sum = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 off = ivec2(i & 1, i >> 1);
		ivec2 p = base + off;
		if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, p_size))) {
			continue;
		}
		float w = (off.x == 1 ? f.x : 1.0 - f.x) * (off.y == 1 ? f.y : 1.0 - f.y);
		if (w <= 0.0) {
			continue;
		}
		float pd;
		vec3 pn;
		float pr;
		unpack_surface(imageLoad(prev_surface, p).xy, pd, pn, pr);
		if (pd <= 0.0 || dot(pn, p_normal) < 0.8) {
			continue;
		}
		vec2 puv = (vec2(p) + 0.5) * params.size.zw;
		vec3 prev_view = vec3(puv * params.previous_view_ray.xy + params.previous_view_ray.zw, -1.0) * pd;
		vec3 in_current = (params.previous_to_current_view * vec4(prev_view, 1.0)).xyz;
		if (abs(dot(p_view_normal, in_current - p_view_pos)) > plane_tolerance) {
			continue;
		}
		r_specular += w * texelFetch(prev_specular_history, p, 0);
		weight_sum += w;
	}
	if (weight_sum > 1e-3) {
		r_specular /= weight_sum;
	}
	return weight_sum;
}

// Catmull-Rom resampling with five bilinear taps. Bilinear resampling blurs
// the history a little every frame, which adds up while the camera moves.
vec3 sample_catmull_rom(sampler2D p_tex, vec2 p_uv) {
	vec2 sample_pos = p_uv * params.size.xy;
	vec2 p1 = floor(sample_pos - 0.5) + 0.5;
	vec2 f = sample_pos - p1;
	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);
	vec2 w12 = w1 + w2;
	vec2 p12 = (p1 + w2 / w12) * params.size.zw;
	vec2 p0 = (p1 - 1.0) * params.size.zw;
	vec2 p3 = (p1 + 2.0) * params.size.zw;
	vec3 r = textureLod(p_tex, vec2(p12.x, p0.y), 0.0).rgb * (w12.x * w0.y);
	r += textureLod(p_tex, vec2(p0.x, p12.y), 0.0).rgb * (w0.x * w12.y);
	r += textureLod(p_tex, p12, 0.0).rgb * (w12.x * w12.y);
	r += textureLod(p_tex, vec2(p3.x, p12.y), 0.0).rgb * (w3.x * w12.y);
	r += textureLod(p_tex, vec2(p12.x, p3.y), 0.0).rgb * (w12.x * w3.y);
	float w = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return r / w;
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size.xy);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	float ndc_depth = imageLoad(depth_image, pos).r;
	if (ndc_depth <= 0.0) {
		// Sky: nothing to denoise (the sky is in the clean part).
		imageStore(diffuse_history, pos, vec4(0.0));
		imageStore(specular_history, pos, vec4(0.0));
		imageStore(moments_history, pos, vec4(0.0));
		imageStore(surface, pos, uvec4(0u));
		imageStore(specular_hit_history, pos, vec4(0.0));
		imageStore(raw_luminance, pos, vec4(0.0));
		return;
	}

	vec2 uv = (vec2(pos) + 0.5) * params.size.zw;
	vec3 vpos = view_position(uv, ndc_depth);
	float linear_depth = -vpos.z;

	uvec4 g = imageLoad(guide_image, pos);
	vec3 normal = guide_normal(g);
	float roughness = guide_roughness(g);

	// Demodulate: texture detail never goes through the filter.
	vec3 diffuse = sanitize(imageLoad(diffuse_image, pos).rgb) / guide_diffuse_albedo(g);
	vec3 specular = sanitize(imageLoad(specular_image, pos).rgb) / guide_specular_albedo(g);

	imageStore(surface, pos, uvec4(pack_surface(linear_depth, normal, roughness), 0u, 0u));

	// Reproject with the motion vector (prev_uv - curr_uv) and validate each
	// of the four bilinear taps against the surface stored last frame.
	vec4 prev_diffuse = vec4(0.0);
	vec4 prev_specular = vec4(0.0);
	vec4 prev_moments = vec4(0.0);
	float prev_hit = 0.0;
	float weight_sum = 0.0;
	int valid_taps = 0;
	vec3 d_min = vec3(1e30);
	vec3 d_max = vec3(0.0);
	vec3 s_min = vec3(1e30);
	vec3 s_max = vec3(0.0);
	vec2 prev_uv = uv;
	if (params.history.w > 0.5) {
		prev_uv = uv + imageLoad(velocity_image, pos).xy;
		vec2 prev_pixel = prev_uv * params.size.xy - 0.5;
		ivec2 base = ivec2(floor(prev_pixel));
		vec2 f = prev_pixel - vec2(base);

		// Where the current point sits in last frame's view (camera motion only).
		vec4 pp = params.current_to_previous_view * vec4(vpos, 1.0);
		float expected_depth = -pp.z;
		float depth_tolerance = 0.03 * expected_depth + 0.01;

		for (int i = 0; i < 4; i++) {
			ivec2 off = ivec2(i & 1, i >> 1);
			ivec2 p = base + off;
			if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) {
				continue;
			}
			float w = (off.x == 1 ? f.x : 1.0 - f.x) * (off.y == 1 ? f.y : 1.0 - f.y);
			if (w <= 0.0) {
				continue;
			}
			float pd;
			vec3 pn;
			float pr;
			unpack_surface(imageLoad(prev_surface, p).xy, pd, pn, pr);
			if (pd <= 0.0 || abs(pd - expected_depth) > depth_tolerance || dot(pn, normal) < 0.9) {
				continue;
			}
			vec4 dh = texelFetch(prev_diffuse_history, p, 0);
			vec4 sh = texelFetch(prev_specular_history, p, 0);
			prev_diffuse += w * dh;
			prev_specular += w * sh;
			prev_moments += w * imageLoad(prev_moments_history, p);
			prev_hit += w * imageLoad(prev_specular_hit_history, p).r;
			weight_sum += w;
			valid_taps++;
			d_min = min(d_min, dh.rgb);
			d_max = max(d_max, dh.rgb);
			s_min = min(s_min, sh.rgb);
			s_max = max(s_max, sh.rgb);
		}
	}

	float diffuse_length = 0.0;
	float specular_length = 0.0;
	if (weight_sum > 1e-3) {
		prev_diffuse /= weight_sum;
		prev_specular /= weight_sum;
		prev_moments /= weight_sum;
		prev_hit /= weight_sum;
		diffuse_length = prev_diffuse.a;
		specular_length = prev_specular.a;
		// All four taps on the same surface: resample the colors sharper,
		// clamped to the taps against ringing.
		if (valid_taps == 4) {
			prev_diffuse.rgb = clamp(sample_catmull_rom(prev_diffuse_history, prev_uv), d_min, d_max);
			prev_specular.rgb = clamp(sample_catmull_rom(prev_specular_history, prev_uv), s_min, s_max);
		}
	}

	// Lighting changes (moving lights and emitters, shadows): the temporal
	// gradient measured by replaying samples (A-SVGF) gives the fraction of
	// the history that is outdated (lambda: change / brightness). Without a
	// gradient (no replayed sample nearby) the history is trusted.
	vec4 gradient = imageLoad(gradient_image, pos / 3);
	float diffuse_lambda = gradient.y > 1e-5 ? clamp(abs(gradient.x) / gradient.y, 0.0, 1.0) : 0.0;
	float specular_lambda = gradient.w > 1e-5 ? clamp(abs(gradient.z) / gradient.w, 0.0, 1.0) : 0.0;

	// Under camera or object motion the history can also be stale in ways a
	// gradient at a fixed surface point doesn't see (resampling, view
	// dependent reflections). There, the history is also compared with this
	// frame's 3x3 neighborhood mean: further away than the neighborhood's
	// noise explains shortens it in proportion, from the next frame on (in
	// the same frame it would weigh samples by their own value). Still pixels
	// skip this: on skewed path tracing noise it darkens by several percent.
	float diffuse_keep = 1.0;
	float specular_keep = 1.0;
	if (length((prev_uv - uv) * params.size.xy) > 0.25 && (diffuse_length > 1.0 || specular_length > 1.0)) {
		float d_sum = 0.0;
		float d_sq = 0.0;
		float s_sum = 0.0;
		float s_sq = 0.0;
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				ivec2 p = clamp(pos + ivec2(x, y), ivec2(0), size - 1);
				uvec4 gq = imageLoad(guide_image, p);
				float ld = luminance(sanitize(imageLoad(diffuse_image, p).rgb) / guide_diffuse_albedo(gq));
				float ls = luminance(sanitize(imageLoad(specular_image, p).rgb) / guide_specular_albedo(gq));
				d_sum += ld;
				d_sq += ld * ld;
				s_sum += ls;
				s_sq += ls * ls;
			}
		}
		float d_mean = d_sum / 9.0;
		float s_mean = s_sum / 9.0;
		float d_tolerance = sqrt(max(d_sq / 9.0 - d_mean * d_mean, 0.0)) + 0.05 * d_mean + 1e-4;
		float s_tolerance = sqrt(max(s_sq / 9.0 - s_mean * s_mean, 0.0)) + 0.05 * s_mean + 1e-4;
		diffuse_keep = min(1.0, d_tolerance / max(abs(luminance(prev_diffuse.rgb) - d_mean), 1e-6));
		specular_keep = min(1.0, s_tolerance / max(abs(luminance(prev_specular.rgb) - s_mean), 1e-6));
	}
	imageStore(raw_luminance, pos, vec4(luminance(sanitize(imageLoad(diffuse_image, pos).rgb)), luminance(sanitize(imageLoad(specular_image, pos).rgb)), 0.0, 0.0));

	// Specular hit distance: traced along the mirror direction on smooth
	// surfaces, sampled (noisy, only when the specular lobe was picked) on
	// rough ones. Accumulated so a frame without a sample keeps the old value.
	float hit = guide_mirror_hit(g);
	if (hit < 0.0) {
		hit = imageLoad(specular_image, pos).a;
	}
	if (hit > 0.0) {
		hit = prev_hit > 0.0 ? mix(prev_hit, hit, 0.5) : hit;
	} else {
		hit = prev_hit;
	}
	imageStore(specular_hit_history, pos, vec4(hit));

	// Smooth surfaces: the reflection moves like the virtual image of the hit
	// point, not like the surface. Blend between both by roughness.
	// Refracted light has no single virtual image: use the surface motion.
	const bool transmissive = guide_transmissive(g);
	float surface_motion = transmissive ? 1.0 : smoothstep(0.0, params.specular_params.x, roughness);
	if (params.history.w > 0.5 && surface_motion < 1.0 && hit > 0.0) {
		vec4 virtual_specular;
		vec3 vn = normal * mat3(params.view_to_world_rotation);
		float wv = reproject_virtual(vpos, vn, normal, hit, uv, size, virtual_specular);
		if (wv > 1e-3) {
			prev_specular = weight_sum > 1e-3 ? mix(virtual_specular, prev_specular, surface_motion) : virtual_specular;
			specular_length = prev_specular.a;
		} else {
			// The reflection was not visible here last frame: start over.
			specular_length = min(specular_length, 1.0 + 2.0 * surface_motion);
		}
	}

	// Mirror-like reflections keep a shorter history than rough ones.
	float max_specular = transmissive ? 16.0 : mix(8.0, params.history.y, smoothstep(0.0, 0.4, roughness));
	diffuse_length = min(diffuse_length + 1.0, params.history.x);
	specular_length = min(specular_length + 1.0, max_specular);

	float diffuse_alpha = mix(1.0 / diffuse_length, 1.0, diffuse_lambda);
	float specular_alpha = mix(1.0 / specular_length, 1.0, specular_lambda);
	float diffuse_moment_alpha = max(diffuse_alpha, 1.0 / min(diffuse_length, params.history.z));
	float specular_moment_alpha = max(specular_alpha, 1.0 / min(specular_length, params.history.z));

	float ld = luminance(diffuse);
	float ls = luminance(specular);
	vec4 moments = vec4(ld, ld * ld, ls, ls * ls);
	moments.xy = mix(prev_moments.xy, moments.xy, diffuse_moment_alpha);
	moments.zw = mix(prev_moments.zw, moments.zw, specular_moment_alpha);

	// The history length follows the blend weight actually used.
	imageStore(diffuse_history, pos, vec4(mix(prev_diffuse.rgb, diffuse, diffuse_alpha), max(1.0, diffuse_keep / diffuse_alpha)));
	imageStore(specular_history, pos, vec4(mix(prev_specular.rgb, specular, specular_alpha), max(1.0, specular_keep / specular_alpha)));
	imageStore(moments_history, pos, moments);
}

#endif // MODE_TEMPORAL

#if defined(MODE_VARIANCE) || defined(MODE_ATROUS)

layout(rg32ui, set = 1, binding = 0) uniform restrict readonly uimage2D surface;

struct Surface {
	float depth; // Linear, 0 = sky.
	vec3 normal; // World space.
	float roughness;
	vec3 position; // View space.
};

Surface load_surface(ivec2 p) {
	Surface s;
	unpack_surface(imageLoad(surface, p).xy, s.depth, s.normal, s.roughness);
	// The view ray at unit depth is affine in the pixel position (perspective).
	vec2 uv = (vec2(p) + 0.5) * params.size.zw;
	s.position = vec3(uv * params.view_ray.xy + params.view_ray.zw, -1.0) * s.depth;
	return s;
}

vec3 view_normal(Surface s) {
	return s.normal * mat3(params.view_to_world_rotation); // Transposed rotation: world to view.
}

// Edge-stopping weight from geometry: distance to the center's tangent plane
// (relative to the pixel footprint at that distance) and normal similarity.
// p_center_view_normal is view_normal(c).
float geometry_weight(Surface c, vec3 p_center_view_normal, Surface q, float distance_pixels) {
	if (q.depth <= 0.0) {
		return 0.0;
	}
	float plane_distance = abs(dot(p_center_view_normal, q.position - c.position));
	float footprint = params.filter_params.w * c.depth * max(distance_pixels, 1.0);
	float w_depth = exp(-plane_distance / (params.filter_params.z * footprint + 1e-4));
	float w_normal = pow(max(dot(c.normal, q.normal), 0.0), params.filter_params.y);
	return w_depth * w_normal;
}

#endif

#ifdef MODE_VARIANCE

layout(rgba16f, set = 1, binding = 1) uniform restrict readonly image2D diffuse_history;
layout(rgba16f, set = 1, binding = 2) uniform restrict readonly image2D specular_history;
layout(rgba16f, set = 1, binding = 3) uniform restrict readonly image2D moments_history;
layout(rgba16f, set = 1, binding = 4) uniform restrict writeonly image2D diffuse_out;
layout(rgba16f, set = 1, binding = 5) uniform restrict writeonly image2D specular_out;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size.xy);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}
	Surface c = load_surface(pos);
	if (c.depth <= 0.0) {
		imageStore(diffuse_out, pos, vec4(0.0));
		imageStore(specular_out, pos, vec4(0.0));
		return;
	}
	vec3 cvn = view_normal(c);
	vec4 d = imageLoad(diffuse_history, pos);
	vec4 s = imageLoad(specular_history, pos);
	vec4 m = imageLoad(moments_history, pos);

	float diffuse_variance = max(m.y - m.x * m.x, 0.0);
	float specular_variance = max(m.w - m.z * m.z, 0.0);

	// Too little history for temporal moments: estimate them from the
	// neighborhood instead (same geometry only).
	if (min(d.a, s.a) < 4.0) {
		vec4 sum_m = vec4(0.0);
		float sum_w = 0.0;
		for (int y = -2; y <= 2; y++) {
			for (int x = -2; x <= 2; x++) {
				ivec2 p = pos + ivec2(x, y);
				if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) {
					continue;
				}
				float w = (x == 0 && y == 0) ? 1.0 : geometry_weight(c, cvn, load_surface(p), length(vec2(x, y)));
				float ld = luminance(imageLoad(diffuse_history, p).rgb);
				float ls = luminance(imageLoad(specular_history, p).rgb);
				sum_m += w * vec4(ld, ld * ld, ls, ls * ls);
				sum_w += w;
			}
		}
		sum_m /= max(sum_w, 1e-4);
		// Few samples: be generous, so the spatial filter does more work.
		float boost = 4.0;
		if (d.a < 4.0) {
			diffuse_variance = max(sum_m.y - sum_m.x * sum_m.x, 0.0) * boost / max(d.a, 1.0);
		}
		if (s.a < 4.0) {
			specular_variance = max(sum_m.w - sum_m.z * sum_m.z, 0.0) * boost / max(s.a, 1.0);
		}
	}

	// The filter works on the accumulated mean, whose variance shrinks with
	// the number of frames in it.
	diffuse_variance /= max(d.a, 1.0);
	specular_variance /= max(s.a, 1.0);

	imageStore(diffuse_out, pos, vec4(d.rgb, diffuse_variance));
	imageStore(specular_out, pos, vec4(s.rgb, specular_variance));
}

#endif // MODE_VARIANCE

#ifdef MODE_ATROUS

layout(rgba16f, set = 1, binding = 1) uniform restrict readonly image2D diffuse_in;
layout(rgba16f, set = 1, binding = 2) uniform restrict readonly image2D specular_in;
layout(rgba16f, set = 1, binding = 3) uniform restrict writeonly image2D diffuse_out;
layout(rgba16f, set = 1, binding = 4) uniform restrict writeonly image2D specular_out;
// History (debug view of the history length).
layout(rgba16f, set = 1, binding = 5) uniform restrict readonly image2D diffuse_history;
layout(rgba16f, set = 1, binding = 6) uniform restrict readonly image2D specular_history;
// Compose (last iteration).
layout(rgba16f, set = 1, binding = 7) uniform restrict readonly image2D base_image;
layout(rgba32ui, set = 1, binding = 8) uniform restrict readonly uimage2D guide_image;
layout(rgba16f, set = 1, binding = 9) uniform restrict readonly image2D raw_diffuse_image;
layout(rgba16f, set = 1, binding = 10) uniform restrict readonly image2D raw_specular_image;
layout(rgba16f, set = 1, binding = 11) uniform restrict writeonly image2D output_image;
layout(r16f, set = 1, binding = 12) uniform restrict readonly image2D specular_hit_history;

// How far (in pixels) the reflection of this surface is blurred by its
// roughness: the lobe (GGX alpha as an angle) spreads over hit distance *
// alpha at the reflected point, seen from depth + hit distance away. The
// specular filter doesn't go wider than that, so mirror-like reflections stay
// sharp and near reflections (contact) stay sharper than far ones.
float specular_radius_pixels(float p_roughness, float p_depth, float p_hit) {
	float alpha = p_roughness * p_roughness;
	float hit = p_hit > 0.0 ? p_hit : 1e4;
	return hit * alpha / ((p_depth + hit) * params.filter_params.w);
}

vec3 heatmap(float t) {
	t = clamp(t, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size.xy);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	Surface c = load_surface(pos);
	vec4 d_center = imageLoad(diffuse_in, pos);
	vec4 s_center = imageLoad(specular_in, pos);

	vec4 d_result = d_center;
	vec4 s_result = s_center;

	if (c.depth > 0.0) {
		float ld = luminance(d_center.rgb);
		float ls = luminance(s_center.rgb);
		float sigma_l = params.filter_params.x;
		float d_sigma = sigma_l * sqrt(d_center.a) + 1e-4;
		float s_sigma = sigma_l * sqrt(s_center.a) + 1e-4;
		vec3 cvn = view_normal(c);
		// After the first iteration, stop where the remaining noise is already
		// below a small fraction of the signal (converged or bright pixels).
		const float SKIP_RELATIVE_NOISE = 0.02;
		bool filter_diffuse = pc.iteration == 0 || sqrt(d_center.a) > SKIP_RELATIVE_NOISE * (ld + 1e-3);
		float s_radius = specular_radius_pixels(c.roughness, c.depth, imageLoad(specular_hit_history, pos).r);
		if (guide_transmissive(imageLoad(guide_image, pos))) {
			// Paths through glass are very noisy at low sample counts; a few
			// pixels of blur are less visible than the noise.
			s_radius = max(s_radius, 4.0);
		}
		bool filter_specular = (pc.iteration == 0 || float(pc.step_size) <= s_radius) &&
				(pc.iteration == 0 || sqrt(s_center.a) > SKIP_RELATIVE_NOISE * (ls + 1e-3));

		const float kernel[2] = float[](1.0, 0.5);
		// Gather the 3x3 taps once: geometry weights and colors.
		vec4 dq[9];
		vec4 sq[9];
		float wgeo[9];
		float wspec[9];
		for (int i = 0; i < 9; i++) {
			ivec2 o = ivec2(i % 3 - 1, i / 3 - 1);
			wgeo[i] = 0.0;
			wspec[i] = 0.0;
			dq[i] = d_center;
			sq[i] = s_center;
			if (i == 4) {
				wgeo[i] = 1.0;
				wspec[i] = 1.0;
			} else if (filter_diffuse || filter_specular) {
				ivec2 p = pos + o * pc.step_size;
				if (all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, size))) {
					Surface q = load_surface(p);
					float wg = geometry_weight(c, cvn, q, float(pc.step_size) * length(vec2(o)));
					if (wg > 1e-4) {
						float k = kernel[abs(o.x)] * kernel[abs(o.y)];
						if (filter_diffuse) {
							wgeo[i] = k * wg;
							dq[i] = imageLoad(diffuse_in, p);
						}
						if (filter_specular) {
							wspec[i] = k * wg * exp(-abs(q.roughness - c.roughness) * 10.0);
							sq[i] = imageLoad(specular_in, p);
						}
					}
				}
			}
		}
		// The first iteration (3x3) uses geometry only: comparing luminance on
		// the noisiest data favors the darker samples of skewed path tracing
		// noise and darkens the image.
		if (pc.iteration == 0) {
			d_sigma = 1e6;
			s_sigma = 1e6;
		}

		vec3 d_sum = vec3(0.0);
		float d_var = 0.0;
		float d_w = 0.0;
		vec3 s_sum = vec3(0.0);
		float s_var = 0.0;
		float s_w = 0.0;
		for (int i = 0; i < 9; i++) {
			float wd = wgeo[i] * exp(-abs(luminance(dq[i].rgb) - ld) / d_sigma);
			d_sum += wd * dq[i].rgb;
			d_var += wd * wd * dq[i].a;
			d_w += wd;
			float ws = wspec[i] * exp(-abs(luminance(sq[i].rgb) - ls) / s_sigma);
			s_sum += ws * sq[i].rgb;
			s_var += ws * ws * sq[i].a;
			s_w += ws;
		}
		if (d_w <= 1e-6) {
			d_sum = d_center.rgb;
			d_var = d_center.a;
			d_w = 1.0;
		}
		if (s_w <= 1e-6) {
			s_sum = s_center.rgb;
			s_var = s_center.a;
			s_w = 1.0;
		}
		d_result = vec4(d_sum / d_w, d_var / (d_w * d_w));
		s_result = vec4(s_sum / s_w, s_var / (s_w * s_w));

	}

	if ((pc.flags & PC_FLAG_COMPOSE) == 0u) {
		imageStore(diffuse_out, pos, d_result);
		imageStore(specular_out, pos, s_result);
		return;
	}

	// Compose: remodulate and add the clean part.
	vec3 base = imageLoad(base_image, pos).rgb;
	vec3 color = base;
	vec3 diffuse = vec3(0.0);
	vec3 specular = vec3(0.0);
	if (c.depth > 0.0) {
		uvec4 g = imageLoad(guide_image, pos);
		diffuse = d_result.rgb * guide_diffuse_albedo(g);
		specular = s_result.rgb * guide_specular_albedo(g);
		color += diffuse + specular;
	}

	vec3 raw = base + imageLoad(raw_diffuse_image, pos).rgb + imageLoad(raw_specular_image, pos).rgb;
	switch (pc.debug_mode) {
		case DEBUG_CLEAN:
			color = base;
			break;
		case DEBUG_DIFFUSE:
			color = diffuse;
			break;
		case DEBUG_SPECULAR:
			color = specular;
			break;
		case DEBUG_RAW:
			color = raw;
			break;
		case DEBUG_HISTORY_LENGTH: {
			float n = c.depth > 0.0 ? imageLoad(diffuse_history, pos).a : 0.0;
			color = heatmap(n / max(params.history.x, 1.0));
		} break;
		case DEBUG_VARIANCE:
			color = vec3(sqrt(d_result.a), sqrt(s_result.a), 0.0);
			break;
		case DEBUG_SPLIT:
			if (pos.x < size.x / 2) {
				color = raw;
			}
			if (pos.x == size.x / 2) {
				color = vec3(1.0);
			}
			break;
		default:
			break;
	}
	imageStore(output_image, pos, vec4(color, 1.0));
}

#endif // MODE_ATROUS

#ifdef MODE_REFERENCE

layout(rgba16f, set = 1, binding = 0) uniform restrict readonly image2D base_image;
layout(rgba16f, set = 1, binding = 1) uniform restrict readonly image2D diffuse_image;
layout(rgba16f, set = 1, binding = 2) uniform restrict readonly image2D specular_image;
layout(rgba32f, set = 1, binding = 3) uniform restrict image2D accumulator;
layout(rgba16f, set = 1, binding = 4) uniform restrict writeonly image2D output_image;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, ivec2(params.size.xy)))) {
		return;
	}
	vec3 color = sanitize(imageLoad(base_image, pos).rgb + imageLoad(diffuse_image, pos).rgb + imageLoad(specular_image, pos).rgb);
	vec4 acc = (pc.flags & PC_FLAG_RESET) != 0u ? vec4(0.0) : imageLoad(accumulator, pos);
	acc.a += 1.0;
	acc.rgb += (color - acc.rgb) / acc.a;
	imageStore(accumulator, pos, acc);
	imageStore(output_image, pos, vec4(acc.rgb, 1.0));
}

#endif // MODE_REFERENCE

// --- Temporal gradients (A-SVGF) ----------------------------------------------

#if defined(MODE_GRADIENT_CLEAR) || defined(MODE_FORWARD_PROJECT) || defined(MODE_GRADIENT) || defined(MODE_GRADIENT_FILTER)

#define GRADIENT_TILE 3

ivec2 gradient_tiles() {
	return (ivec2(params.size.xy) + GRADIENT_TILE - 1) / GRADIENT_TILE;
}

#endif

#ifdef MODE_GRADIENT_CLEAR

layout(r32ui, set = 1, binding = 0) uniform restrict writeonly uimage2D gradient_claim;
layout(rgba32ui, set = 1, binding = 1) uniform restrict writeonly uimage2D gradient_sample;

void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(tile, gradient_tiles()))) {
		return;
	}
	imageStore(gradient_claim, tile, uvec4(0u));
	imageStore(gradient_sample, tile, uvec4(0u));
}

#endif // MODE_GRADIENT_CLEAR

#ifdef MODE_FORWARD_PROJECT

layout(rg32ui, set = 1, binding = 0) uniform restrict readonly uimage2D prev_surface;
layout(r32ui, set = 1, binding = 1) uniform restrict readonly uimage2D seed_image; // Last frame's seeds.
layout(r32ui, set = 1, binding = 2) uniform restrict uimage2D gradient_claim;
layout(rgba32ui, set = 1, binding = 3) uniform restrict writeonly uimage2D gradient_sample;
layout(rgba32f, set = 1, binding = 4) uniform restrict writeonly image2D gradient_target;

uint hash(uint x) {
	x ^= x >> 16u;
	x *= 0x7feb352du;
	x ^= x >> 15u;
	x *= 0x846ca68bu;
	x ^= x >> 16u;
	return x;
}

// One thread per tile of the last frame: a random pixel of it moves to where
// its surface point is now (camera motion), and claims that tile.
void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size.xy);
	if (any(greaterThanEqual(tile, gradient_tiles()))) {
		return;
	}
	uint h = hash(uint(tile.x) + uint(tile.y) * 4099u + uint(pc.iteration) * 16777619u);
	ivec2 q = tile * GRADIENT_TILE + ivec2(h % 3u, (h / 3u) % 3u);
	if (any(greaterThanEqual(q, size))) {
		return;
	}
	float depth;
	vec3 normal;
	float roughness;
	unpack_surface(imageLoad(prev_surface, q).xy, depth, normal, roughness);
	if (depth <= 0.0) {
		return;
	}
	vec2 quv = (vec2(q) + 0.5) * params.size.zw;
	vec3 prev_view = vec3(quv * params.previous_view_ray.xy + params.previous_view_ray.zw, -1.0) * depth;
	vec3 view = (params.previous_to_current_view * vec4(prev_view, 1.0)).xyz;
	if (view.z > -1e-3) {
		return;
	}
	vec2 uv = project_uv(params.projection_unjittered, view);
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) {
		return;
	}
	ivec2 p = ivec2(uv * params.size.xy);
	ivec2 target_tile = p / GRADIENT_TILE;
	ivec2 in_tile = p % GRADIENT_TILE;
	if (imageAtomicCompSwap(gradient_claim, target_tile, 0u, 1u) == 0u) {
		uint packed = (1u << 31u) | uint(in_tile.x + GRADIENT_TILE * in_tile.y);
		imageStore(gradient_sample, target_tile, uvec4(packed, uint(q.x + q.y * size.x), imageLoad(seed_image, q).x, 0u));
		imageStore(gradient_target, target_tile, vec4(view, length(view)));
	}
}

#endif // MODE_FORWARD_PROJECT

#ifdef MODE_GRADIENT

layout(r32ui, set = 1, binding = 0) uniform restrict writeonly uimage2D gradient_claim;
layout(rgba32ui, set = 1, binding = 1) uniform restrict uimage2D gradient_sample;
layout(rgba16f, set = 1, binding = 2) uniform restrict readonly image2D diffuse_image;
layout(rgba16f, set = 1, binding = 3) uniform restrict readonly image2D specular_image;
layout(rg16f, set = 1, binding = 4) uniform restrict readonly image2D prev_raw_luminance;
layout(rgba16f, set = 1, binding = 5) uniform restrict writeonly image2D gradient_out;

void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size.xy);
	if (any(greaterThanEqual(tile, gradient_tiles()))) {
		return;
	}
	uvec4 gs = imageLoad(gradient_sample, tile);
	vec4 result = vec4(0.0, -1.0, 0.0, -1.0);
	// Only samples whose replayed ray hit the projected point (w = 1).
	if ((gs.x >> 31u) != 0u && gs.w == 1u) {
		uint offset = gs.x & 0xFu;
		ivec2 p = tile * GRADIENT_TILE + ivec2(offset % 3u, offset / 3u);
		ivec2 q = ivec2(gs.y % uint(size.x), gs.y / uint(size.x));
		vec2 prev = imageLoad(prev_raw_luminance, q).xy;
		float d = luminance(sanitize(imageLoad(diffuse_image, p).rgb));
		float s = luminance(sanitize(imageLoad(specular_image, p).rgb));
		result = vec4(d - prev.x, max(d, prev.x), s - prev.y, max(s, prev.y));
	}
	imageStore(gradient_out, tile, result);
	// Empty the tile for the next frame's forward projection.
	imageStore(gradient_sample, tile, uvec4(0u));
	imageStore(gradient_claim, tile, uvec4(0u));
}

#endif // MODE_GRADIENT

#ifdef MODE_GRADIENT_FILTER

layout(rgba16f, set = 1, binding = 0) uniform restrict readonly image2D gradient_in;
layout(rgba16f, set = 1, binding = 1) uniform restrict writeonly image2D gradient_out;
layout(r32f, set = 1, binding = 2) uniform restrict readonly image2D depth_image;

float tile_depth(ivec2 p_tile) {
	ivec2 p = min(p_tile * GRADIENT_TILE + 1, ivec2(params.size.xy) - 1);
	float ndc = imageLoad(depth_image, p).r;
	return ndc > 0.0 ? -view_position((vec2(p) + 0.5) * params.size.zw, ndc).z : 0.0;
}

// The gradient samples are sparse (one per tile, where one could be replayed)
// and noisy; the change and the brightness are averaged separately over
// neighboring tiles of similar depth.
void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	ivec2 tiles = gradient_tiles();
	if (any(greaterThanEqual(tile, tiles))) {
		return;
	}
	float z = tile_depth(tile);
	vec4 sum = vec4(0.0);
	vec2 weight = vec2(0.0);
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 t = tile + ivec2(x, y) * pc.step_size;
			if (any(lessThan(t, ivec2(0))) || any(greaterThanEqual(t, tiles))) {
				continue;
			}
			vec4 g = imageLoad(gradient_in, t);
			float k = (x == 0 ? 1.0 : 0.5) * (y == 0 ? 1.0 : 0.5);
			float zt = tile_depth(t);
			k *= (z > 0.0 && zt > 0.0) ? exp(-abs(zt - z) / (0.05 * z)) : (z == zt ? 1.0 : 0.0);
			if (g.y >= 0.0) {
				sum.xy += k * g.xy;
				weight.x += k;
			}
			if (g.w >= 0.0) {
				sum.zw += k * g.zw;
				weight.y += k;
			}
		}
	}
	vec4 result = vec4(0.0, -1.0, 0.0, -1.0);
	if (weight.x > 1e-4) {
		result.xy = sum.xy / weight.x;
	}
	if (weight.y > 1e-4) {
		result.zw = sum.zw / weight.y;
	}
	imageStore(gradient_out, tile, result);
}

#endif // MODE_GRADIENT_FILTER
