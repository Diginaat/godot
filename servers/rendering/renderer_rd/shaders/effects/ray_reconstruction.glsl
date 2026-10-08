#[compute]

#version 450

#VERSION_DEFINES

// Native ray reconstruction (path tracer denoiser), an independent
// implementation of spatiotemporal variance-guided filtering (Schied et al.
// 2017). See docs/renderer/native_ray_reconstruction.md.
//
// Passes (one shader, one mode each):
//   MODE_TEMPORAL  demodulate, reproject, validate and accumulate history
//   MODE_VARIANCE  per-pixel variance (spatial estimate for short history)
//   MODE_ATROUS    one edge-aware a-trous iteration; the first one feeds the
//                  history, the last one composes the final image

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
}
params;

layout(push_constant, std430) uniform PushConstant {
	int step_size; // A-trous step in pixels.
	int iteration;
	uint flags; // PC_FLAG_*
	uint debug_mode;
}
pc;

#define PC_FLAG_FEEDBACK 1u // Write this iteration's result into the history.
#define PC_FLAG_COMPOSE 2u // Last iteration: write the final image.

#define DEBUG_NONE 0u
#define DEBUG_CLEAN 1u
#define DEBUG_DIFFUSE 2u
#define DEBUG_SPECULAR 3u
#define DEBUG_RAW 4u
#define DEBUG_HISTORY_LENGTH 5u
#define DEBUG_VARIANCE 6u
#define DEBUG_SPLIT 7u

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
	return float(g.w & 0xFFFFu) / 65535.0;
}

vec3 guide_diffuse_albedo(uvec4 g) {
	return max(unpackUnorm4x8(g.x).rgb, vec3(ALBEDO_MIN));
}

vec3 guide_specular_albedo(uvec4 g) {
	return max(unpackUnorm4x8(g.y).rgb, vec3(ALBEDO_MIN));
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

layout(rgba16f, set = 1, binding = 5) uniform restrict readonly image2D prev_diffuse_history;
layout(rgba16f, set = 1, binding = 6) uniform restrict readonly image2D prev_specular_history;
layout(rgba16f, set = 1, binding = 7) uniform restrict readonly image2D prev_moments_history;
layout(rg32ui, set = 1, binding = 8) uniform restrict readonly uimage2D prev_surface;

layout(rgba16f, set = 1, binding = 9) uniform restrict writeonly image2D diffuse_history;
layout(rgba16f, set = 1, binding = 10) uniform restrict writeonly image2D specular_history;
layout(rgba16f, set = 1, binding = 11) uniform restrict writeonly image2D moments_history;
layout(rg32ui, set = 1, binding = 12) uniform restrict writeonly uimage2D surface;

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
	float weight_sum = 0.0;
	if (params.history.w > 0.5) {
		vec2 prev_uv = uv + imageLoad(velocity_image, pos).xy;
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
			prev_diffuse += w * imageLoad(prev_diffuse_history, p);
			prev_specular += w * imageLoad(prev_specular_history, p);
			prev_moments += w * imageLoad(prev_moments_history, p);
			weight_sum += w;
		}
	}

	float diffuse_length = 0.0;
	float specular_length = 0.0;
	if (weight_sum > 1e-3) {
		prev_diffuse /= weight_sum;
		prev_specular /= weight_sum;
		prev_moments /= weight_sum;
		diffuse_length = prev_diffuse.a;
		specular_length = prev_specular.a;
	}

	// Mirror-like reflections move with the reflected scene, not the surface:
	// keep their history short (step 4 reprojects them properly).
	float max_specular = mix(2.0, params.history.y, smoothstep(0.05, 0.5, roughness));
	diffuse_length = min(diffuse_length + 1.0, params.history.x);
	specular_length = min(specular_length + 1.0, max_specular);

	float diffuse_alpha = 1.0 / diffuse_length;
	float specular_alpha = 1.0 / specular_length;
	float diffuse_moment_alpha = 1.0 / min(diffuse_length, params.history.z);
	float specular_moment_alpha = 1.0 / min(specular_length, params.history.z);

	float ld = luminance(diffuse);
	float ls = luminance(specular);
	vec4 moments = vec4(ld, ld * ld, ls, ls * ls);
	moments.xy = mix(prev_moments.xy, moments.xy, diffuse_moment_alpha);
	moments.zw = mix(prev_moments.zw, moments.zw, specular_moment_alpha);

	imageStore(diffuse_history, pos, vec4(mix(prev_diffuse.rgb, diffuse, diffuse_alpha), diffuse_length));
	imageStore(specular_history, pos, vec4(mix(prev_specular.rgb, specular, specular_alpha), specular_length));
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
// Feedback into the history (first iteration) keeps the history length in alpha.
layout(rgba16f, set = 1, binding = 5) uniform restrict image2D diffuse_history;
layout(rgba16f, set = 1, binding = 6) uniform restrict image2D specular_history;
// Compose (last iteration).
layout(rgba16f, set = 1, binding = 7) uniform restrict readonly image2D base_image;
layout(rgba32ui, set = 1, binding = 8) uniform restrict readonly uimage2D guide_image;
layout(rgba16f, set = 1, binding = 9) uniform restrict readonly image2D raw_diffuse_image;
layout(rgba16f, set = 1, binding = 10) uniform restrict readonly image2D raw_specular_image;
layout(rgba16f, set = 1, binding = 11) uniform restrict writeonly image2D output_image;

// Roughness below which the specular signal isn't filtered at this iteration:
// mirror-like reflections stay sharp, rough ones get the full kernel.
float specular_min_roughness(int iteration) {
	return 0.02 * float(1 << iteration);
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
		bool filter_specular = c.roughness >= specular_min_roughness(pc.iteration) &&
				(pc.iteration == 0 || sqrt(s_center.a) > SKIP_RELATIVE_NOISE * (ls + 1e-3));

		const float kernel[2] = float[](1.0, 0.5);
		vec3 d_sum = d_center.rgb;
		float d_var = d_center.a;
		float d_w = 1.0;
		vec3 s_sum = s_center.rgb;
		float s_var = s_center.a;
		float s_w = 1.0;

		for (int y = -1; y <= 1 && (filter_diffuse || filter_specular); y++) {
			for (int x = -1; x <= 1; x++) {
				if (x == 0 && y == 0) {
					continue;
				}
				ivec2 p = pos + ivec2(x, y) * pc.step_size;
				if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) {
					continue;
				}
				Surface q = load_surface(p);
				float wg = geometry_weight(c, cvn, q, float(pc.step_size) * length(vec2(x, y)));
				if (wg <= 1e-4) {
					continue;
				}
				float k = kernel[abs(x)] * kernel[abs(y)];

				if (filter_diffuse) {
					vec4 dq = imageLoad(diffuse_in, p);
					float wd = k * wg * exp(-abs(luminance(dq.rgb) - ld) / d_sigma);
					d_sum += wd * dq.rgb;
					d_var += wd * wd * dq.a;
					d_w += wd;
				}

				if (filter_specular) {
					vec4 sq = imageLoad(specular_in, p);
					float w_rough = exp(-abs(q.roughness - c.roughness) * 10.0);
					float ws = k * wg * w_rough * exp(-abs(luminance(sq.rgb) - ls) / s_sigma);
					s_sum += ws * sq.rgb;
					s_var += ws * ws * sq.a;
					s_w += ws;
				}
			}
		}
		d_result = vec4(d_sum / d_w, d_var / (d_w * d_w));
		s_result = vec4(s_sum / s_w, s_var / (s_w * s_w));

		if ((pc.flags & PC_FLAG_FEEDBACK) != 0u) {
			imageStore(diffuse_history, pos, vec4(d_result.rgb, imageLoad(diffuse_history, pos).a));
			imageStore(specular_history, pos, vec4(s_result.rgb, imageLoad(specular_history, pos).a));
		}
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
