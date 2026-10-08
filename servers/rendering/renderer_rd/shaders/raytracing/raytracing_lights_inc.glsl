// Light sampling and Next Event Estimation (NEE) for raytracing.
// Requires: raytracing_inc.glsl, brdf_inc.glsl, tlas at binding 1, payload at location 0.
// Note: ray_query_alpha_test() still requires GL_EXT_ray_query (used by DLSS-RR path or when USE_RAY_QUERY_SHADOWS is defined).

// ============================================================================
// Light Types and Constants
// ============================================================================

#define RT_LIGHT_TYPE_OMNI 0 // Point light with radius (soft shadows)
#define RT_LIGHT_TYPE_DIRECTIONAL 1 // Sun/moon with angular size
#define RT_LIGHT_TYPE_SPOT 3 // Spot light with cone falloff

// Reservoir sampling batch size for stochastic light selection.
#ifndef RT_LIGHT_RESERVOIR_SIZE
#define RT_LIGHT_RESERVOIR_SIZE 16
#endif

// ============================================================================
// Light Data (matches C++ RT_LightData, 80 bytes, std430)
// ============================================================================

struct RTLightData {
	vec3 position; // World pos (omni/spot) or direction (directional).
	uint type; // RT_LIGHT_TYPE_*.
	vec3 emission; // HDR emission color (color * energy).
	float radius; // Light size (omni/spot) or angular radius (directional).
	float attenuation; // Attenuation exponent (2=inverse-square, 1=linear).
	float inv_max_range; // 1/range (-1 = infinite).
	float max_range_squared; // range^2 (0 = infinite).
	float specular_amount; // Godot specular multiplier [0..1].
	float indirect_energy; // Godot indirect energy multiplier.
	float inv_spot_attenuation; // Spot cone softness.
	float cos_spot_angle; // Cosine of spot cone half-angle.
	float _pad0;
	vec3 spot_direction; // Spot direction (normalized, world space).
	float _pad1;
};

// Light buffer SSBO (binding provided by the including shader via RT_LIGHT_BUFFER_BINDING).
#ifndef RT_LIGHT_BUFFER_BINDING
#define RT_LIGHT_BUFFER_BINDING 13
#endif

layout(set = 0, binding = RT_LIGHT_BUFFER_BINDING, std430) readonly buffer LightBuffer {
	RTLightData rt_lights[];
};

// ============================================================================
// Emissive meshes sampled as lights (matches C++ RT_EmissiveMeshData, 96 bytes)
// ============================================================================

struct EmissiveMeshData {
	vec4 object_to_world[3]; // Rows of the object-to-world 3x4 (includes the compression AABB).
	vec3 center; // World-space bounds center.
	float radius; // World-space bounds radius.
	uint geometry_idx; // Index into geometries[] and materials[].
	uint primitive_count; // Triangle count.
	float power; // Emission luminance times surface area estimate.
	float _pad;
	vec3 half_extents; // World-space bounds half size (AABB around center).
	float _pad2;
};

layout(set = 0, binding = 33, std430) readonly buffer EmissiveMeshBuffer {
	EmissiveMeshData rt_emissive_meshes[];
};

// Below this roughness, emissive meshes are left to BRDF sampling (sharp
// reflections of emitters); at or above it, NEE samples them.
#define RT_MESH_LIGHT_MIN_ROUGHNESS 0.3

// ============================================================================
// Unified Cone Sampling (for sphere, directional, spot lights)
// ============================================================================

struct LightSample {
	vec3 cone_axis; // Direction to sample around (normalized).
	float cos_theta_max; // Cone half-angle cosine (1.0 = point, 0.0 = hemisphere).
	vec3 emission; // Light radiance.
	float distance_sq; // Squared distance to light (0 = directional).
	float max_distance; // Max shadow ray distance.
};

// Build orthonormal basis from a single direction.
void lights_build_basis(vec3 dir, out vec3 tangent, out vec3 bitangent) {
	vec3 up = abs(dir.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	tangent = normalize(cross(up, dir));
	bitangent = cross(dir, tangent);
}

// Transform local direction to world space around axis.
vec3 lights_local_to_world(vec3 local_dir, vec3 axis) {
	vec3 tangent, bitangent;
	lights_build_basis(axis, tangent, bitangent);
	return local_dir.x * tangent + local_dir.y * bitangent + local_dir.z * axis;
}

// Prepare unified cone sample from any light type.
LightSample lights_prepare_sample(vec3 hit_pos, RTLightData light) {
	LightSample s;

	vec3 to_light = light.position - hit_pos;
	float dist_sq = dot(to_light, to_light);
	float inv_dist = inversesqrt(dist_sq + 1e-10);
	float dist = dist_sq * inv_dist;

	float is_directional = (light.type == RT_LIGHT_TYPE_DIRECTIONAL) ? 1.0 : 0.0;

	// Cone axis.
	vec3 sphere_axis = to_light * inv_dist;
	vec3 dir_axis = -normalize(light.position);
	s.cone_axis = mix(sphere_axis, dir_axis, is_directional);

	// Distance (0 for directional = no falloff).
	s.distance_sq = mix(dist_sq, 0.0, is_directional);

	// Cone angle from subtended solid angle.
	float sin_theta_sphere = clamp(light.radius * inv_dist, 0.0, 1.0);
	float cos_theta_sphere = sqrt(max(0.0, 1.0 - sin_theta_sphere * sin_theta_sphere));
	float cos_theta_dir = cos(light.radius);
	s.cos_theta_max = mix(cos_theta_sphere, cos_theta_dir, is_directional);
	s.cos_theta_max = min(s.cos_theta_max, 0.999999);

	s.emission = light.emission;

	// Max shadow ray distance.
	float sphere_max = dist + light.radius;
	float dir_max = 10000.0;
	s.max_distance = mix(sphere_max, dir_max, is_directional);

	return s;
}

// Sample a direction within the light's cone.
vec3 lights_sample_cone(LightSample ls, vec2 u, out float pdf) {
	float cos_theta = 1.0 - u.x * (1.0 - ls.cos_theta_max);
	float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
	float phi = 2.0 * PI * u.y;

	vec3 local_dir = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
	vec3 L = lights_local_to_world(local_dir, ls.cone_axis);

	float solid_angle = 2.0 * PI * (1.0 - ls.cos_theta_max);
	pdf = 1.0 / max(solid_angle, 1e-10);

	return L;
}

// ============================================================================
// Attenuation
// ============================================================================

// Godot-style windowed distance attenuation.
// window = (1 - (d/range)^4)^2, combined with pow(d, -decay).
float lights_get_attenuation(LightSample ls, float inv_max_range, float decay) {
	if (ls.distance_sq <= 0.0) {
		return 1.0; // Directional: no distance attenuation.
	}

	float distance = sqrt(ls.distance_sq);
	float atten = min(pow(max(distance, 0.0001), -decay), 1.0);

	// Windowed falloff if range is finite (inv_max_range >= 0).
	if (inv_max_range >= 0.0) {
		float nd = distance * inv_max_range;
		nd *= nd;
		nd *= nd; // nd^4
		nd = max(1.0 - nd, 0.0);
		nd *= nd; // nd^2 window
		return atten * nd;
	}

	return atten;
}

// Per-light specular multiplier.
float lights_get_specular_multiplier(float specular_amount, float roughness) {
	if (specular_amount >= 0.0) {
		return specular_amount;
	} else {
		float r3 = roughness * roughness * roughness;
		return mix(0.0, r3, -specular_amount);
	}
}

// ============================================================================
// Inline Alpha Test (shared by all ray query proceed loops)
// ============================================================================

/// Inline alpha test for ray query candidates. Returns true if the hit is opaque (alpha >= 0.5).
/// Mirrors the any-hit shader logic for use with inline ray queries.
bool ray_query_alpha_test(uint geometry_idx, uint primitive_id, vec2 candidate_bary) {
	vec3 bary = vec3(1.0 - candidate_bary.x - candidate_bary.y, candidate_bary.x, candidate_bary.y);

	GeometryData geom = geometries[geometry_idx];
	uint i0, i1, i2;
	get_triangle_indices_ex(geom, primitive_id, i0, i1, i2);
	vec2 uv = fetch_uv(geom, i0, i1, i2, bary);

	MaterialData mat = materials[geometry_idx];
	uv = uv * mat.uv1_scale + mat.uv1_offset;
	float alpha = texture(sampler2D(bindless_textures[nonuniformEXT(mat.albedo_texture_idx)], SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), uv).a;
	alpha *= mat.albedo_color.a;

	return alpha >= material_alpha_threshold(mat.flags);
}

// ============================================================================
// Shadow Ray (traceRayEXT pipeline)
// ============================================================================

/// Returns true if light is visible.
/// Uses SkipClosestHitShader so only any_hit (alpha test) and miss are invoked.
/// TerminateOnFirstHit causes early exit on first confirmed opaque hit.
bool lights_trace_shadow_ray(vec3 origin, vec3 direction, float max_dist, inout uint rng_state) {
#ifdef USE_RAY_QUERY_SHADOWS
	// Ray queries are significantly faster, but can not handle complex alpha materials
	rayQueryEXT shadow_rq;
	rayQueryInitializeEXT(shadow_rq, tlas,
			gl_RayFlagsTerminateOnFirstHitEXT,
			0xFF, origin, 0.001, direction, max_dist - 0.001);

	while (rayQueryProceedEXT(shadow_rq)) {
		if (rayQueryGetIntersectionTypeEXT(shadow_rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
			// quick and dirty way to check transparency by sampling the alpha texture
			// this completely ignores the actual material, so might not be accurate
			if (ray_query_alpha_test(
						rayQueryGetIntersectionInstanceCustomIndexEXT(shadow_rq, false),
						rayQueryGetIntersectionPrimitiveIndexEXT(shadow_rq, false),
						rayQueryGetIntersectionBarycentricsEXT(shadow_rq, false))) {
				rayQueryConfirmIntersectionEXT(shadow_rq);
			}
		}
	}

	return rayQueryGetIntersectionTypeEXT(shadow_rq, true) == gl_RayQueryCommittedIntersectionNoneEXT;
#elif defined(USE_SER)
	hitObjectEXT hitObject;
	hitObjectTraceRayEXT(hitObject, tlas,
			gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
			0xFF, 0, 0, 0,
			origin, 0.001, direction, max_dist - 0.001, 0);

	return !(hitObjectIsHitEXT(hitObject));
#else
	/// The miss shader writes radiance = vec3(1.0) for shadow rays (visible).
	/// If an opaque hit occurs, miss is never called and radiance stays vec3(0.0).
	// Save full payload, set up shadow ray, then restore after trace.
	PathPayload saved_payload = payload;

	PathState shadow_ps;
	shadow_ps.radiance = vec3(0.0);
	shadow_ps.throughput = vec3(0.0);
	shadow_ps.packed_bounces_flags = set_shadow_ray(0u);
	shadow_ps.rng_state = 0u;
	shadow_ps.hit_t = 0.0;
	shadow_ps.offset_normal = vec3(0.0, 0.0, 1.0);
	shadow_ps.next_ray_dir = vec3(0.0, 0.0, 1.0);
	path_pack(payload, shadow_ps);

	traceRayEXT(tlas,
			gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
			0xFF, 0, 0, 0,
			origin, 0.001, direction, max_dist - 0.001, 0);

	// Unpack to check visibility (miss shader packs radiance = 1.0).
	shadow_ps = path_unpack(payload);
	bool visible = shadow_ps.radiance.x > 0.5;

	payload = saved_payload;

	return visible;
#endif
}

// ============================================================================
// Next Event Estimation (NEE) - Direct Light Sampling
// ============================================================================

// Estimated unshadowed contribution of a light at a surface, used only to
// choose which light to sample. It must be > 0 wherever the light can add
// light, or the estimate becomes biased; it doesn't have to be exact.
float lights_selection_weight(RTLightData light, vec3 hit_pos, vec3 N) {
	float power = luminance(light.emission);
	if (power <= 0.0) {
		return 0.0;
	}

	if (light.type == RT_LIGHT_TYPE_DIRECTIONAL) {
		// A sun disk of angular radius r still reaches N while cos > -sin(r).
		float cos_l = dot(N, -normalize(light.position));
		return power * max(cos_l + sin(light.radius), 0.0);
	}

	vec3 to_light = light.position - hit_pos;
	float dist_sq = dot(to_light, to_light);
	if (light.max_range_squared != 0.0 && dist_sq > light.max_range_squared) {
		return 0.0;
	}
	float inv_dist = inversesqrt(max(dist_sq, 1e-10));
	vec3 L = to_light * inv_dist;

	LightSample ls_atten;
	ls_atten.distance_sq = dist_sq;
	float atten = lights_get_attenuation(ls_atten, light.inv_max_range, light.attenuation);

	// A sphere light of angular radius a still reaches N while cos > -sin(a).
	float sin_a = clamp(light.radius * inv_dist, 0.0, 1.0);
	float w = power * atten * max(dot(N, L) + sin_a, 0.0);

	if (light.type == RT_LIGHT_TYPE_SPOT && dot(-L, light.spot_direction) <= light.cos_spot_angle) {
		// Center outside the cone. Part of a large light can still be inside, so
		// keep a small weight instead of zero.
		w *= 0.01;
	}
	return w;
}

// Selection weight for an emissive mesh, from its bounding sphere. Like
// lights_selection_weight(), it is > 0 wherever the mesh can add light.
float lights_mesh_selection_weight(EmissiveMeshData em, vec3 hit_pos, vec3 N) {
	vec3 to_center = em.center - hit_pos;
	// No point of the mesh lies in front of the surface when every corner of
	// its bounds is behind it (the dot product is linear, so this is exact).
	// The sphere bound below can't tell: a wide panel just under a roof pokes
	// through the roof plane, and the roof would waste samples on it.
	if (dot(N, to_center) + dot(abs(N), em.half_extents) <= 0.0) {
		return 0.0;
	}
	float dist_sq = dot(to_center, to_center);
	float radius_sq = em.radius * em.radius;
	float cos_w = 1.0; // Inside the bounds the mesh can be in any direction.
	if (dist_sq > radius_sq) {
		float inv_dist = inversesqrt(dist_sq);
		float sin_a = clamp(em.radius * inv_dist, 0.0, 1.0);
		cos_w = max(dot(N, to_center * inv_dist) + sin_a, 0.0);
	}
	return em.power * scene_data_block.data.emissive_exposure_normalization * cos_w / max(dist_sq, max(radius_sq, 1e-4));
}

// Object-space vertex position (normalized to the compression AABB for compressed meshes).
vec3 lights_fetch_object_position(in GeometryData geom, uint idx) {
	if ((geom.flags & FLAG_COMPRESSED) != 0u) {
		Uint32Buffer vb = Uint32Buffer(geom.vertex_address);
		uint w0 = vb.v[idx * 2u];
		uint w1 = vb.v[idx * 2u + 1u];
		return vec3(float(w0 & 0xFFFFu), float(w0 >> 16u), float(w1 & 0xFFFFu)) / 65535.0;
	}
	FloatBuffer fb = FloatBuffer(geom.vertex_address);
	uint base = idx * (geom.position_stride >> 2u);
	return vec3(fb.v[base], fb.v[base + 1u], fb.v[base + 2u]);
}

vec3 lights_mesh_to_world(EmissiveMeshData em, vec3 p) {
	vec4 p4 = vec4(p, 1.0);
	return vec3(dot(em.object_to_world[0], p4), dot(em.object_to_world[1], p4), dot(em.object_to_world[2], p4));
}

// Direct light from one emissive mesh: picks a triangle uniformly and a point
// uniformly on it. Returns the contribution before dividing by the light
// selection PDF.
vec3 lights_sample_emissive_mesh(uint mesh_idx, vec3 hit_pos, vec3 N, vec3 V, MaterialProperties material, inout uint rng_state) {
	EmissiveMeshData em = rt_emissive_meshes[mesh_idx];
	GeometryData geom = geometries[em.geometry_idx];
	if (geom.vertex_address == 0ul || em.primitive_count == 0u) {
		return vec3(0.0);
	}

	uint triangle = min(uint(rand(rng_state) * float(em.primitive_count)), em.primitive_count - 1u);
	uint i0, i1, i2;
	get_triangle_indices_ex(geom, triangle, i0, i1, i2);
	vec3 p0 = lights_mesh_to_world(em, lights_fetch_object_position(geom, i0));
	vec3 p1 = lights_mesh_to_world(em, lights_fetch_object_position(geom, i1));
	vec3 p2 = lights_mesh_to_world(em, lights_fetch_object_position(geom, i2));

	vec3 cross_e = cross(p1 - p0, p2 - p0);
	float twice_area = length(cross_e);
	if (twice_area < 1e-12) {
		return vec3(0.0);
	}

	// Uniform point on the triangle.
	vec2 u = rand2(rng_state);
	float su = sqrt(u.x);
	vec3 bary = vec3(1.0 - su, su * (1.0 - u.y), su * u.y);
	vec3 P = p0 * bary.x + p1 * bary.y + p2 * bary.z;

	vec3 to_light = P - hit_pos;
	float dist_sq = dot(to_light, to_light);
	if (dist_sq < 1e-8) {
		return vec3(0.0);
	}
	float dist = sqrt(dist_sq);
	vec3 L = to_light / dist;
	if (dot(N, L) <= 0.0) {
		return vec3(0.0);
	}
	// Two-sided, like emission seen by a ray hit.
	float cos_light = abs(dot(cross_e / twice_area, L));
	if (cos_light < 1e-4) {
		return vec3(0.0);
	}

	// Emitted radiance at the point, as the closest hit computes it for HG0.
	MaterialData mat = materials[em.geometry_idx];
	vec3 Le = mat.emission_color * mat.emission_strength;
	if ((mat.flags & 2u) != 0u) {
		vec2 uv = fetch_uv(geom, i0, i1, i2, bary) * mat.uv1_scale + mat.uv1_offset;
		Le *= texture(sampler2D(bindless_textures[nonuniformEXT(mat.emission_texture_idx)], SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT), uv).rgb;
	}
	Le *= scene_data_block.data.emissive_exposure_normalization;
	if (max(Le.r, max(Le.g, Le.b)) <= 0.0) {
		return vec3(0.0);
	}

	// Stop the shadow ray just short of the emitter so it doesn't hit itself.
	if (!lights_trace_shadow_ray(hit_pos, L, dist * 0.999, rng_state)) {
		return vec3(0.0);
	}

	vec3 brdf_diffuse, brdf_specular;
	evalCombinedBRDFSeparate(N, L, V, material, brdf_diffuse, brdf_specular);

	// Area PDF is 1 / (triangle_count * area); converting to solid angle
	// multiplies by dist^2 / cos_light.
	float area = 0.5 * twice_area;
	return (brdf_diffuse + brdf_specular) * Le * (cos_light * float(em.primitive_count) * area / dist_sq);
}

// Evaluate direct lighting using NEE with stochastic light selection.
// Selects one light by resampled importance sampling (lights_selection_weight).
vec3 lights_evaluate_direct_lighting(
		vec3 hit_pos,
		vec3 N,
		vec3 V,
		MaterialProperties material,
		inout uint rng_state,
		bool is_indirect_bounce,
		uint light_count,
		uint mesh_count) {
	// Candidates 0..light_count-1 are analytic lights, the rest emissive meshes.
	const uint total_count = light_count + mesh_count;
	if (total_count == 0u) {
		return vec3(0.0);
	}

	// Pick one light with probability proportional to its estimated unshadowed
	// contribution (resampled importance sampling). With few lights every light
	// is a candidate; with many, RT_LIGHT_RESERVOIR_SIZE random candidates.
	// Uniform selection used to pick a far spot light as often as the sun, which
	// left directly lit pixels black when the sun was never picked.
	const bool enumerate_all = total_count <= uint(RT_LIGHT_RESERVOIR_SIZE);
	const uint candidate_count = enumerate_all ? total_count : uint(RT_LIGHT_RESERVOIR_SIZE);
	float weight_sum = 0.0;
	float selected_weight = 0.0;
	uint selected_idx = 0u;

	for (uint i = 0u; i < candidate_count; i++) {
		uint idx = enumerate_all ? i : min(uint(rand(rng_state) * float(total_count)), total_count - 1u);
		float w = (idx < light_count)
				? lights_selection_weight(rt_lights[idx], hit_pos, N)
				: lights_mesh_selection_weight(rt_emissive_meshes[idx - light_count], hit_pos, N);
		if (w <= 0.0) {
			continue;
		}
		weight_sum += w;
		if (rand(rng_state) * weight_sum < w) {
			selected_idx = idx;
			selected_weight = w;
		}
	}

	if (weight_sum <= 0.0) {
		return vec3(0.0);
	}

	// RIS with uniformly drawn candidates: the effective selection PDF is
	// w / (total_count / candidate_count * weight_sum). With enumerate_all this
	// is exactly w / weight_sum.
	float light_select_pdf = selected_weight * float(candidate_count) / (float(total_count) * weight_sum);

	if (selected_idx >= light_count) {
		return lights_sample_emissive_mesh(selected_idx - light_count, hit_pos, N, V, material, rng_state) / max(light_select_pdf, 1e-10);
	}

	RTLightData light = rt_lights[selected_idx];
	vec2 u = rand2(rng_state);

	// === POSITIONAL LIGHT PATH (omni + spot) ===
	if (light.type == RT_LIGHT_TYPE_OMNI || light.type == RT_LIGHT_TYPE_SPOT) {
		vec3 to_light = light.position - hit_pos;
		float dist_sq = dot(to_light, to_light);

		// Early out: outside max range.
		if (light.max_range_squared != 0.0 && dist_sq > light.max_range_squared) {
			return vec3(0.0);
		}

		float dist = sqrt(dist_sq);
		vec3 L;
		float shadow_dist;

		if (light.radius <= 0.01) {
			// True point light: exact direction.
			L = to_light / max(dist, 0.0001);
			shadow_dist = dist;
		} else {
			// Sphere light: cone sampling for soft shadows.
			LightSample ls = lights_prepare_sample(hit_pos, light);
			float light_pdf;
			L = lights_sample_cone(ls, u, light_pdf);
			float t_center = dot(to_light, L);
			vec3 perp = to_light - t_center * L;
			float perp_sq = dot(perp, perp);
			float dt = sqrt(max(0.0, light.radius * light.radius - perp_sq));
			shadow_dist = max(0.0, t_center - dt);
		}

		// Spot cone early-out.
		float spot_atten = 1.0;
		if (light.type == RT_LIGHT_TYPE_SPOT) {
			float scos = dot(-L, light.spot_direction);
			if (scos <= light.cos_spot_angle) {
				return vec3(0.0);
			}
			float spot_rim = max(1e-4, (1.0 - scos) / (1.0 - light.cos_spot_angle));
			spot_atten = 1.0 - pow(spot_rim, light.inv_spot_attenuation);
		}

		if (!lights_trace_shadow_ray(hit_pos, L, shadow_dist, rng_state)) {
			return vec3(0.0);
		}

		// Evaluate BRDF (diffuse + specular separately for specular_amount control).
		vec3 brdf_diffuse, brdf_specular;
		evalCombinedBRDFSeparate(N, L, V, material, brdf_diffuse, brdf_specular);

		// Distance attenuation.
		LightSample ls_atten;
		ls_atten.distance_sq = dist_sq;
		float atten = lights_get_attenuation(ls_atten, light.inv_max_range, light.attenuation) * spot_atten;

		float spec_mul = lights_get_specular_multiplier(light.specular_amount, material.roughness);
		vec3 brdf_value = brdf_diffuse + brdf_specular * spec_mul;

		float indirect_mul = is_indirect_bounce ? light.indirect_energy : 1.0;

		// NdotL is already included in brdf_value (evalLambertian/evalMicrofacet bake it in).
		vec3 contribution = brdf_value * light.emission * atten * indirect_mul;
		return contribution / max(light_select_pdf, 1e-10);
	}
	// === CONE LIGHT PATH (directional) ===
	else {
		LightSample ls = lights_prepare_sample(hit_pos, light);
		float light_pdf;
		vec3 L = lights_sample_cone(ls, u, light_pdf);

		float NdotL = dot(N, L);
		if (NdotL <= 0.0) {
			return vec3(0.0);
		}

		if (!lights_trace_shadow_ray(hit_pos, L, ls.max_distance, rng_state)) {
			return vec3(0.0);
		}

		vec3 brdf_diffuse, brdf_specular;
		evalCombinedBRDFSeparate(N, L, V, material, brdf_diffuse, brdf_specular);

		float spec_mul = lights_get_specular_multiplier(light.specular_amount, material.roughness);
		vec3 brdf_value = brdf_diffuse + brdf_specular * spec_mul;

		float indirect_mul = is_indirect_bounce ? light.indirect_energy : 1.0;

		return brdf_value * light.emission * indirect_mul / max(light_select_pdf, 1e-10);
	}
}
