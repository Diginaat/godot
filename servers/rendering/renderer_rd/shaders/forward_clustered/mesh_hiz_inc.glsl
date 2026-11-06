// Conservative HZB box test shared by CLOD group and MultiMesh instance
// occlusion. The includer declares `depth_pyramid`: the reverse-Z farthest
// (minimum) depth pyramid, power-of-two padded with clear depth zero.

// True only if the box is proven behind the depth pyramid. Every uncertain
// case (clipping, near plane, non-finite bounds, screen edges, ties) is visible.
// `error` bounds the float rounding of `local_to_clip * point` per clip axis.
bool hzb_box_occluded(vec3 origin, vec3 size, mat4 local_to_clip, vec4 error, ivec2 depth_size) {
	vec2 uv_min = vec2(1.0);
	vec2 uv_max = vec2(0.0);
	float nearest_depth = 0.0;
	if (any(isnan(error)) || any(isinf(error))) {
		return false;
	}
	for (uint corner = 0u; corner < 8u; corner++) {
		vec3 point = origin + size * vec3(float(corner & 1u), float((corner >> 1u) & 1u), float((corner >> 2u) & 1u));
		vec4 clip = local_to_clip * vec4(point, 1.0);
		if (any(isnan(clip)) || any(isinf(clip)) || clip.w - error.w <= 0.0001) {
			return false;
		}
		vec3 a = (clip.xyz - error.xyz) / (clip.w - error.w);
		vec3 b = (clip.xyz - error.xyz) / (clip.w + error.w);
		vec3 c = (clip.xyz + error.xyz) / (clip.w - error.w);
		vec3 d = (clip.xyz + error.xyz) / (clip.w + error.w);
		if (any(isnan(a)) || any(isinf(a)) || any(isnan(b)) || any(isinf(b)) || any(isnan(c)) || any(isinf(c)) || any(isnan(d)) || any(isinf(d))) {
			return false;
		}
		vec3 ndc_min = min(min(a, b), min(c, d));
		vec3 ndc_max = max(max(a, b), max(c, d));
		// Clipped/near-plane-crossing bounds must remain visible. No corner-only
		// projection is trusted for a box crossing the eye or near/far clip plane.
		if (ndc_min.z < 0.0 || ndc_max.z > 1.0) {
			return false;
		}
		uv_min = min(uv_min, ndc_min.xy * 0.5 + 0.5);
		uv_max = max(uv_max, ndc_max.xy * 0.5 + 0.5);
		nearest_depth = max(nearest_depth, ndc_max.z);
	}
	if (any(lessThan(uv_min, vec2(0.0))) || any(greaterThan(uv_max, vec2(1.0)))) {
		return false;
	}
	ivec2 first = max(ivec2(floor(uv_min * vec2(depth_size))) - 1, ivec2(0));
	ivec2 last = min(ivec2(ceil(uv_max * vec2(depth_size))) + 1, depth_size - 1);
	int level = 0;
	while (any(greaterThan((last >> level) - (first >> level), ivec2(1)))) {
		level++;
	}
	ivec2 low = first >> level;
	ivec2 high = last >> level;
	float farthest_occluder = 1.0;
	for (int y = low.y; y <= high.y; y++) {
		for (int x = low.x; x <= high.x; x++) {
			farthest_occluder = min(farthest_occluder, texelFetch(depth_pyramid, ivec2(x, y), level).r);
		}
	}
	return nearest_depth + 0.00001 < farthest_occluder;
}
