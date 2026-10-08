// ReSTIR DI (reservoir-based spatiotemporal importance resampling) for direct
// light at the primary hit. Bitterli et al. 2020, "Spatiotemporal reservoir
// resampling for real-time ray tracing with dynamic direct lighting"; Lin et
// al. 2022, "Generalized resampled importance sampling"; see also NVIDIA RTXDI.
//
// A sample is a light plus the random numbers that pick a point on it, so any
// pixel can re-evaluate it: the target function is the luminance of that
// light's unshadowed estimator (lights_eval_light() / lights_eval_emissive_mesh())
// at a surface. Each pixel resamples
//   - its own candidate, the light lights_select() picks,
//   - last frame's reservoir at the reprojected pixel (temporal),
//   - optionally a few of last frame's reservoirs around it (spatial; off,
//     see RESTIR_SPATIAL_SAMPLES),
// with generalized balance heuristic weights: candidate i's weight is
// M_i p_i(y) / sum_j M_j p_j(y) over all merged surfaces j (neighbors judged
// by the cheap light selection weight). The weights sum to one, which keeps the
// result unbiased up to that approximation and keeps reused weights from
// growing. Measured: plain 1/M weights with visibility reuse were 28% too
// dark, an M count limited to surfaces the sample can reach let rare large
// weights feed on themselves across frames (spreading fireflies).
//
// No visibility reuse: a sample shadowed at its own pixel keeps its weight for
// others; the sign of the stored W records the shadow for the pixel's later
// samples, which reuse sample 0's result (see shade_and_bounce()).
//
// Requires: raytracing_lights_inc.glsl and the bindings declared in
// raytracing_common_inc.glsl under USE_RESTIR_DI.

#ifdef USE_RESTIR_DI

#define RESTIR_INVALID_KEY 0xFFFFFFFFu
#define RESTIR_MESH_BIT 0x80000000u
#define RESTIR_MESH_SHIFT 22u
#define RESTIR_TRIANGLE_MASK 0x3FFFFFu
// History length cap: higher reuses more, but reacts slower to changes.
#define RESTIR_MAX_M 20.0
// Spatial reuse is off. It only went wrong when light indices changed between
// frames (lights used to be re-sorted every frame) and reservoirs of other
// pixels were reused: then the image brightened frame after frame, even with
// a correct index remap and fresh data (both verified on the GPU). Lights are
// now kept in a stable order, which stops that, but with a moving camera
// spatial reuse still came out 9% dark and no less noisy than temporal reuse
// alone. See PATHTRACER_TESTING.md.
#define RESTIR_SPATIAL_SAMPLES 0u
#define RESTIR_SPATIAL_RADIUS 24.0
// This pixel's candidate plus the temporal and spatial reservoirs.
#define RESTIR_MAX_CANDIDATES (2u + RESTIR_SPATIAL_SAMPLES)

struct RestirReservoir {
	uint key; // Light index, or RESTIR_MESH_BIT | mesh << RESTIR_MESH_SHIFT | triangle.
	vec2 u; // Random numbers choosing the point on the light.
	float M; // Confidence: how many candidates the reservoir stands for.
};

struct RestirCandidates {
	uint key[RESTIR_MAX_CANDIDATES];
	vec2 u[RESTIR_MAX_CANDIDATES];
	float W[RESTIR_MAX_CANDIDATES]; // Unbiased contribution weight of the sample at its own surface.
	float M[RESTIR_MAX_CANDIDATES];
	vec3 pos[RESTIR_MAX_CANDIDATES]; // Surface; entry 0 is this pixel.
	vec3 normal[RESTIR_MAX_CANDIDATES];
	uint count;
};

// Unshadowed estimator of one sample at a surface. Mesh lights are skipped on
// glossy surfaces, where shade_and_bounce() leaves them to the BRDF ray.
vec3 restir_eval(uint key, vec2 u, vec3 hit_pos, vec3 N, vec3 V, MaterialProperties material, bool allow_mesh, out vec3 r_L, out float r_dist) {
	r_L = vec3(0.0, 0.0, 1.0);
	r_dist = 0.0;
	if (key == RESTIR_INVALID_KEY) {
		return vec3(0.0);
	}
	if ((key & RESTIR_MESH_BIT) != 0u) {
		uint mesh = (key & ~RESTIR_MESH_BIT) >> RESTIR_MESH_SHIFT;
		if (!allow_mesh || mesh >= uint(get_rt_param(RT_PARAM_EMISSIVE_MESH_COUNT))) {
			return vec3(0.0);
		}
		return lights_eval_emissive_mesh(mesh, key & RESTIR_TRIANGLE_MASK, u, hit_pos, N, V, material, r_L, r_dist);
	}
	if (key >= uint(get_rt_param(RT_PARAM_LIGHT_COUNT))) {
		return vec3(0.0);
	}
	return lights_eval_light(key, u, hit_pos, N, V, material, false, r_L, r_dist);
}

// Weight function for another pixel's surface in the balance heuristic. Only
// the surface position and normal are stored, so this uses the cheap light
// selection weights (lights_selection_weight()): positive wherever the light
// can reach the surface, which is all the heuristic needs to stay unbiased.
float restir_target_at(uint key, vec3 pos, vec3 normal, bool allow_mesh) {
	if (key == RESTIR_INVALID_KEY) {
		return 0.0;
	}
	if ((key & RESTIR_MESH_BIT) != 0u) {
		uint mesh = (key & ~RESTIR_MESH_BIT) >> RESTIR_MESH_SHIFT;
		if (!allow_mesh || mesh >= uint(get_rt_param(RT_PARAM_EMISSIVE_MESH_COUNT))) {
			return 0.0;
		}
		return lights_mesh_selection_weight(rt_emissive_meshes[mesh], pos, normal);
	}
	if (key >= uint(get_rt_param(RT_PARAM_LIGHT_COUNT))) {
		return 0.0;
	}
	return lights_selection_weight(rt_lights[key], pos, normal);
}

// Last frame's light index for this frame's lights, via the remap buffer.
uint restir_remap_key(uint key) {
	if (key == RESTIR_INVALID_KEY) {
		return key;
	}
	if ((key & RESTIR_MESH_BIT) != 0u) {
		uint mesh = restir_light_remap[RESTIR_LIGHTS_MAX + ((key & ~RESTIR_MESH_BIT) >> RESTIR_MESH_SHIFT)];
		return mesh == RESTIR_INVALID_KEY ? mesh : (RESTIR_MESH_BIT | (mesh << RESTIR_MESH_SHIFT) | (key & RESTIR_TRIANGLE_MASK));
	}
	return restir_light_remap[key];
}

uvec4 restir_load_sample(ivec2 p, bool previous) {
	bool odd = (uint(get_rt_param(RT_PARAM_FRAME_INDEX)) & 1u) != 0u;
	return (odd != previous) ? imageLoad(restir_sample_1, p) : imageLoad(restir_sample_0, p);
}

vec4 restir_load_surface(ivec2 p, bool previous) {
	bool odd = (uint(get_rt_param(RT_PARAM_FRAME_INDEX)) & 1u) != 0u;
	return (odd != previous) ? imageLoad(restir_surface_1, p) : imageLoad(restir_surface_0, p);
}

// Stores this pixel's reservoir for the next frame (sample 0 only). Sample
// layout: key, packed u, W (negative when shadowed at this pixel), M.
void restir_store(uint key, vec2 u, float W, float M, vec3 hit_pos, vec3 N) {
	if (!is_sample_zero(payload.packed_bounces_flags)) {
		return;
	}
	ivec2 p = ivec2(gl_LaunchIDEXT.xy);
	uvec4 s = uvec4(key, packUnorm2x16(u), floatBitsToUint(W), floatBitsToUint(M));
	vec4 surf = vec4(hit_pos, uintBitsToFloat(packUnorm2x16(vec3_to_oct(N))));
	bool odd = (uint(get_rt_param(RT_PARAM_FRAME_INDEX)) & 1u) != 0u;
	if (odd) {
		imageStore(restir_sample_1, p, s);
		imageStore(restir_surface_1, p, surf);
	} else {
		imageStore(restir_sample_0, p, s);
		imageStore(restir_surface_0, p, surf);
	}
}

// Adds last frame's reservoir at pixel p as a candidate, if it lies on a
// surface like this one (same plane within a few percent of the view
// distance, normal within ~25 degrees).
void restir_add_previous(inout RestirCandidates c, ivec2 p, vec3 hit_pos, vec3 N, float view_dist, float max_offset) {
	if (c.count >= RESTIR_MAX_CANDIDATES || any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, ivec2(gl_LaunchSizeEXT.xy)))) {
		return;
	}
	uvec4 s = restir_load_sample(p, true);
	float M = min(uintBitsToFloat(s.w), RESTIR_MAX_M);
	if (!(M > 0.0)) {
		return;
	}
	vec4 surf = restir_load_surface(p, true);
	// vec3_to_oct() returns [0, 1]; oct_to_vec3() takes [-1, 1].
	vec3 prev_N = oct_to_vec3(unpackUnorm2x16(floatBitsToUint(surf.w)) * 2.0 - 1.0);
	vec3 offset = surf.xyz - hit_pos;
	if (dot(prev_N, N) < 0.9 || abs(dot(N, offset)) > 0.02 * view_dist || length(offset) > max_offset * view_dist) {
		return;
	}
	uint i = c.count;
	c.key[i] = restir_remap_key(s.x);
	c.u[i] = unpackUnorm2x16(s.y);
	c.W[i] = abs(uintBitsToFloat(s.z)); // The sign only says "shadowed at its pixel".
	c.M[i] = M;
	c.pos[i] = surf.xyz;
	c.normal[i] = prev_N;
	c.count++;
}

// Resamples the primary hit's direct light: the caller's NEE candidate (its
// unshadowed estimate p_initial, picked with probability p_initial_pdf) plus
// last frame's reservoirs. Returns the unshadowed estimate for the winner
// (already times W) with its shadow ray in r_L / r_dist; r_reservoir and r_W
// are for restir_store() once the caller knows the visibility. Doesn't trace
// rays, so the shadow ray is shared with the plain NEE path (one call site
// keeps warps mixing primary and later hits from running two traces in turn).
vec3 restir_di_resample(bool p_picked, uint p_key, vec2 p_u, float p_initial_pdf, vec3 p_initial, vec3 hit_pos, vec3 N, vec3 V, MaterialProperties material, inout uint rng_state, bool allow_mesh, float view_dist, out RestirReservoir r_reservoir, out float r_W, inout vec3 r_L, inout float r_dist) {
	// Entry 0: this pixel's candidate, from RIS light selection; its unbiased
	// contribution weight is 1 / pick probability. M = 1 even without a pick:
	// this pixel's domain counts in every weight.
	RestirCandidates c;
	c.key[0] = p_picked ? p_key : RESTIR_INVALID_KEY;
	c.u[0] = p_u;
	c.W[0] = p_picked ? 1.0 / max(p_initial_pdf, 1e-10) : 0.0;
	c.M[0] = 1.0;
	c.pos[0] = hit_pos;
	c.normal[0] = N;
	c.count = 1u;

	if (uint(get_rt_param(RT_PARAM_FRAME_INDEX)) > 0u) {
		ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
		vec2 motion = project_uv(hit_pos, prev_vp_unjittered) - project_uv(hit_pos, curr_vp_unjittered);
		ivec2 prev_pixel = pixel + ivec2(round(motion * vec2(gl_LaunchSizeEXT.xy)));
		// Temporal: the same surface point last frame.
		restir_add_previous(c, prev_pixel, hit_pos, N, view_dist, 0.05);
		// Spatial: last frame's neighbors (this frame's are being computed now).
		for (uint i = 0u; i < RESTIR_SPATIAL_SAMPLES; i++) {
			vec2 d = rand2(rng_state);
			float radius = RESTIR_SPATIAL_RADIUS * sqrt(d.x);
			float angle = 2.0 * PI * d.y;
			ivec2 q = prev_pixel + ivec2(round(radius * vec2(cos(angle), sin(angle))));
			restir_add_previous(c, q, hit_pos, N, view_dist, 1.0);
		}
	}

	r_reservoir.key = RESTIR_INVALID_KEY;
	r_reservoir.u = vec2(0.0);
	r_reservoir.M = 0.0;
	r_W = 0.0;
	float weight_sum = 0.0;
	float selected_target = 0.0;
	vec3 selected = vec3(0.0);
	for (uint i = 0u; i < c.count; i++) {
		r_reservoir.M += c.M[i];
		if (c.key[i] == RESTIR_INVALID_KEY || !(c.W[i] > 0.0)) {
			continue;
		}
		vec3 L = r_L;
		float dist = r_dist;
		vec3 here = (i == 0u) ? p_initial : restir_eval(c.key[i], c.u[i], hit_pos, N, V, material, allow_mesh, L, dist);
		float target = luminance(here);
		if (target <= 0.0) {
			continue;
		}
		// Generalized balance heuristic: this pixel uses its own target, the
		// others restir_target_at() (the weights still sum to one).
		float own = (i == 0u) ? target : restir_target_at(c.key[i], c.pos[i], c.normal[i], allow_mesh);
		float denom = target * c.M[0];
		for (uint j = 1u; j < c.count; j++) {
			denom += c.M[j] * ((j == i) ? own : restir_target_at(c.key[i], c.pos[j], c.normal[j], allow_mesh));
		}
		float w = (c.M[i] * own / max(denom, 1e-20)) * target * c.W[i];
		if (!(w > 0.0) || isinf(w)) {
			continue;
		}
		weight_sum += w;
		if (rand(rng_state) * weight_sum < w) {
			r_reservoir.key = c.key[i];
			r_reservoir.u = c.u[i];
			selected_target = target;
			selected = here;
			r_L = L;
			r_dist = dist;
		}
	}

	if (r_reservoir.key == RESTIR_INVALID_KEY || selected_target <= 0.0) {
		r_reservoir.key = RESTIR_INVALID_KEY;
		return vec3(0.0);
	}
	r_W = weight_sum / selected_target;
	return selected * r_W;
}

#endif // USE_RESTIR_DI
