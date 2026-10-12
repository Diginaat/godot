// DDGI functions that read the DDGI resources. Include after ddgi_inc.glsl
// and after declaring:
//   DDGIDataBlock ddgi (in a uniform block)
//   DDGIProbe ddgi_probes[] (storage buffer)
// and, when DDGI_SAMPLING is defined:
//   texture2D ddgi_irradiance_atlas, ddgi_distance_atlas
//   sampler DDGI_SAMPLER (linear, clamp)

// The first `fixed` rays of every probe keep the same directions every update,
// so relocation and classification decisions are stable. The rest are rotated
// randomly each frame for the irradiance and distance estimates.
vec3 ddgi_probe_ray_direction(uint p_ray) {
	uint fixed_rays = ddgi.counts.z;
	if (p_ray < fixed_rays) {
		return ddgi_spherical_fibonacci(p_ray, fixed_rays);
	}
	vec3 d = ddgi_spherical_fibonacci(p_ray - fixed_rays, max(1u, ddgi.counts.y - fixed_rays));
	return normalize(vec3(dot(ddgi.ray_rotation[0].xyz, d), dot(ddgi.ray_rotation[1].xyz, d), dot(ddgi.ray_rotation[2].xyz, d)));
}

// Guided probe rays, after "Real-Time Markov Chain Path Guiding for Global
// Illumination and Single Scattering" (Alber, Hanika, Dachsbacher, 2025):
// where the light arriving at a probe comes mostly from one side (a sunlit
// patch seen from a dark room, a window, a bright emitter), some of the
// rotated rays are sent toward it, sampled from a von Mises-Fisher lobe fitted
// to the probe's radiance (DDGIProbe.guide); the rest stay uniform. Each ray
// then counts with the weight uniform pdf / mixture pdf, so the blend stays an
// unbiased (self-normalized) estimate, with less noise for the same rays.
// Written from the paper; one lobe per probe instead of the paper's Markov
// chain hash grids, since each probe already is a cache point.
#define DDGI_GUIDE_MAX_FRACTION 0.5

// Rays sent toward the lobe, out of p_rotated, for a probe's guide.
uint ddgi_guided_ray_count(vec4 p_guide, uint p_rotated, out vec3 r_axis, out float r_kappa) {
	float mean_length = length(p_guide.xyz);
	r_axis = mean_length > 0.0 ? p_guide.xyz / mean_length : vec3(0.0, 0.0, 1.0);
	// Mean resultant length to concentration (Banerjee et al. approximation).
	float rbar = min(mean_length, 0.95);
	r_kappa = rbar * (3.0 - rbar * rbar) / (1.0 - rbar * rbar);
	// Light from all around (the sky outdoors) gains nothing from guiding.
	float fraction = p_guide.w > 0.0 ? DDGI_GUIDE_MAX_FRACTION * smoothstep(0.2, 0.5, rbar) : 0.0;
	return uint(float(p_rotated) * fraction + 0.5);
}

// Direction of ray p_ray of a probe with guide p_guide (xyz), and its blend
// weight relative to a uniform ray (w; 1 when the probe isn't guided).
vec4 ddgi_probe_ray_direction_weighted(uint p_ray, vec4 p_guide) {
	uint fixed_rays = ddgi.counts.z;
	uint rotated = max(1u, ddgi.counts.y - fixed_rays);
	if (p_ray < fixed_rays) {
		return vec4(ddgi_spherical_fibonacci(p_ray, fixed_rays), 1.0);
	}
	vec3 axis;
	float kappa;
	uint guided = ddgi_guided_ray_count(p_guide, rotated, axis, kappa);
	if (guided == 0u) {
		return vec4(ddgi_probe_ray_direction(p_ray), 1.0);
	}
	uint uniform_count = rotated - guided;
	uint index = p_ray - fixed_rays;
	vec3 dir;
	if (index < uniform_count) {
		vec3 d = ddgi_spherical_fibonacci(index, uniform_count);
		dir = normalize(vec3(dot(ddgi.ray_rotation[0].xyz, d), dot(ddgi.ray_rotation[1].xyz, d), dot(ddgi.ray_rotation[2].xyz, d)));
	} else {
		// A stratified (Fibonacci lattice) sample of the lobe, shifted every
		// frame by the random offsets in the w of ray_rotation.
		uint k = index - uniform_count;
		float u = fract((float(k) + 0.5) / float(guided) + ddgi.ray_rotation[0].w);
		float v = fract(float(k) * 0.61803398875 + ddgi.ray_rotation[1].w);
		float cos_theta = 1.0 + log(u + (1.0 - u) * exp(-2.0 * kappa)) / kappa;
		float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0, 1.0));
		float phi = 6.28318530718 * v;
		vec3 t = normalize(cross(abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), axis));
		vec3 b = cross(axis, t);
		dir = normalize(t * (cos(phi) * sin_theta) + b * (sin(phi) * sin_theta) + axis * cos_theta);
	}
	// Mixture pdf over the uniform pdf (1 / 4 pi): the uniform share plus the
	// guided share times the lobe's pdf, also relative to uniform.
	float lobe = 2.0 * kappa / (1.0 - exp(-2.0 * kappa)) * exp(kappa * (dot(axis, dir) - 1.0));
	float mixture = (float(uniform_count) + float(guided) * lobe) / float(rotated);
	return vec4(dir, 1.0 / mixture);
}

#ifdef DDGI_SAMPLING

// Fraction of the volume's weight at a point: 1 inside, fading to 0 over the
// edge blend width at the borders of the probe grid.
float ddgi_volume_weight(DDGIVolume vol, vec3 p_local) {
	vec3 g = p_local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
	vec3 edge = min(g, vec3(vol.grid.xyz) - 1.0 - g);
	float d = min(edge.x, min(edge.y, edge.z));
	return clamp(d / max(vol.params.w, 0.001), 0.0, 1.0);
}

// Weight of one of the 8 probes around a lookup point (see
// ddgi_sample_volume()): 0 when the probe can't be used. Sets r_untraced for
// probes not traced yet.
float ddgi_probe_weight(DDGIVolume vol, uint p_probe, ivec3 p_logical, ivec3 p_offs, vec3 p_alpha, vec3 p_local, vec3 p_local_normal, inout bool r_untraced) {
	uint state = ddgi_probes[p_probe].state;
	// Scrolled-in probes hold the light of the probes that scrolled out (the
	// far side of the grid) until traced: let a coarser cascade cover them.
	if (state == DDGI_PROBE_NEW || state == DDGI_PROBE_SCROLLED) {
		r_untraced = true;
		return 0.0;
	}
	if (state == DDGI_PROBE_INSIDE) {
		return 0.0;
	}

	vec3 probe_local = ddgi_probe_local_position(vol, p_logical) + ddgi_probes[p_probe].offset;
	vec3 to_probe = probe_local - p_local;
	float dist = length(to_probe);
	vec3 dir = dist > 0.0 ? to_probe / dist : p_local_normal;

	vec3 tri = mix(1.0 - p_alpha, p_alpha, vec3(p_offs));
	float trilinear = tri.x * tri.y * tri.z;

	// Smooth backface: probes behind the surface count less, but never zero,
	// so thin geometry doesn't go black.
	float wrap = (dot(dir, p_local_normal) + 1.0) * 0.5;
	float weight = wrap * wrap + 0.2;

	// Chebyshev visibility from the probe's distance moments, looked up in
	// the direction from the probe to the shading point.
	vec3 world_dir_from_probe = -ddgi_xform_dir(vol.local_to_world, dir);
	vec2 moments = textureLod(sampler2D(ddgi_distance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(p_probe, world_dir_from_probe, ddgi.atlas.y, ddgi.atlas.z, ddgi.atlas_inv_size.zw), 0.0).rg;
	if (dist > moments.x) {
		float variance = abs(moments.x * moments.x - moments.y);
		float d = dist - moments.x;
		float chebyshev = variance / (variance + d * d);
		weight *= max(chebyshev * chebyshev * chebyshev, 0.0);
	}
	weight = max(weight, 0.000001);

	// Crush tiny weights so light doesn't leak through walls.
	const float crush_threshold = 0.2;
	if (weight < crush_threshold) {
		weight *= weight * weight / (crush_threshold * crush_threshold);
	}

	weight *= trilinear;
	// Inactive probes (no surface nearby) still keep their light up to date
	// with the fixed rays. They count a little, so a point whose probes are
	// all inactive or inside gets their light instead of going dark; only
	// probes inside geometry never count.
	if (state == DDGI_PROBE_INACTIVE) {
		weight *= 0.05;
	}
	return weight;
}

// Irradiance (cosine weighted mean radiance) at world position p_pos with
// normal p_normal, seen from direction p_view (unit vector toward the viewer),
// from one volume. Returns rgb and the volume weight in a. When the point is
// inside the volume but none of its 8 probes can be used because they are all
// inside geometry (a narrow cave or tunnel between probes), returns
// black with the negated volume weight: no light is known to reach the point.
// Probes not traced yet (new or scrolled in) leave the point uncovered (a = 0).
vec4 ddgi_sample_volume(uint p_volume, vec3 p_pos, vec3 p_normal, vec3 p_view) {
	DDGIVolume vol = ddgi.volumes[p_volume];

	// Bias the lookup position off the surface, mostly toward the viewer, so
	// the visibility test doesn't self-shadow.
	vec3 pos = p_pos + p_normal * vol.spacing.y + p_view * vol.spacing.z;
	vec3 local = ddgi_xform(vol.world_to_local, pos);

	float volume_weight = ddgi_volume_weight(vol, ddgi_xform(vol.world_to_local, p_pos));
	if (volume_weight <= 0.0) {
		return vec4(0.0);
	}

	vec3 local_normal = ddgi_xform_dir(vol.world_to_local, p_normal);
	vec3 g = local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
	ivec3 base = clamp(ivec3(floor(g)), ivec3(0), vol.grid.xyz - 2);
	vec3 alpha = clamp(g - vec3(base), vec3(0.0), vec3(1.0));

	uint irr_texels = ddgi.atlas.x;
	uint per_row = ddgi.atlas.z;

	vec3 sum = vec3(0.0);
	float weight_sum = 0.0;
	bool untraced = false;

	for (uint i = 0u; i < 8u; i++) {
		ivec3 offs = ivec3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
		ivec3 logical = base + offs;
		uint probe = ddgi_probe_index(vol, logical);
		float weight = ddgi_probe_weight(vol, probe, logical, offs, alpha, local, local_normal, untraced);
		if (weight <= 0.0) {
			continue;
		}

		vec3 irradiance = textureLod(sampler2D(ddgi_irradiance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, p_normal, irr_texels, per_row, ddgi.atlas_inv_size.xy), 0.0).rgb;
		// Blend in a perceptual (square root) space: smoother transitions
		// between bright and dark probes.
		sum += sqrt(max(irradiance, vec3(0.0))) * weight;
		weight_sum += weight;
	}

	if (weight_sum <= 0.0) {
		return vec4(0.0, 0.0, 0.0, untraced ? 0.0 : -volume_weight);
	}
	vec3 result = sum / weight_sum;
	return vec4(result * result, volume_weight);
}

// Irradiance from all volumes. Volumes are sorted finest first; each one
// covers what the previous ones left, so overlaps blend without double
// counting. Returns rgb (normalized) and the total coverage in a.
vec4 ddgi_sample_irradiance(vec3 p_pos, vec3 p_normal, vec3 p_view) {
	vec3 sum = vec3(0.0);
	float remaining = 1.0;
	for (uint v = 0u; v < ddgi.counts.x && v < uint(DDGI_MAX_VOLUMES); v++) {
		vec4 s = ddgi_sample_volume(v, p_pos, p_normal, p_view);
		if (s.a == 0.0) {
			continue;
		}
		// A volume whose probes around the point are all inside geometry counts
		// as black: falling through to the environment's ambient light (or to a
		// coarser cascade, whose probes outside would leak) lit caves with the sky.
		float weight = abs(s.a);
		sum += s.rgb * weight * remaining;
		remaining *= 1.0 - weight;
		if (remaining < 0.001) {
			break;
		}
	}
	float coverage = 1.0 - remaining;
	return coverage > 0.0 ? vec4(sum / coverage, coverage) : vec4(0.0);
}

// ddgi_sample_volume() for DDGI_DIRS lookup directions at once (the low
// resolution apply: the surface normal and directions tilted around it).
// The probe weights come from p_normal and are shared by all directions.
// Returns the volume weight as ddgi_sample_volume() does, the irradiance of
// each direction in r_irradiance.
#define DDGI_DIRS 5
vec4 ddgi_sample_volume_dirs(uint p_volume, vec3 p_pos, vec3 p_normal, vec3 p_view, vec3 p_dirs[DDGI_DIRS], out vec3 r_irradiance[DDGI_DIRS]) {
	for (int k = 0; k < DDGI_DIRS; k++) {
		r_irradiance[k] = vec3(0.0);
	}
	DDGIVolume vol = ddgi.volumes[p_volume];
	vec3 pos = p_pos + p_normal * vol.spacing.y + p_view * vol.spacing.z;
	vec3 local = ddgi_xform(vol.world_to_local, pos);
	float volume_weight = ddgi_volume_weight(vol, ddgi_xform(vol.world_to_local, p_pos));
	if (volume_weight <= 0.0) {
		return vec4(0.0);
	}
	vec3 local_normal = ddgi_xform_dir(vol.world_to_local, p_normal);
	vec3 g = local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
	ivec3 base = clamp(ivec3(floor(g)), ivec3(0), vol.grid.xyz - 2);
	vec3 alpha = clamp(g - vec3(base), vec3(0.0), vec3(1.0));

	vec3 sum[DDGI_DIRS];
	for (int k = 0; k < DDGI_DIRS; k++) {
		sum[k] = vec3(0.0);
	}
	float weight_sum = 0.0;
	bool untraced = false;
	for (uint i = 0u; i < 8u; i++) {
		ivec3 offs = ivec3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
		ivec3 logical = base + offs;
		uint probe = ddgi_probe_index(vol, logical);
		float weight = ddgi_probe_weight(vol, probe, logical, offs, alpha, local, local_normal, untraced);
		if (weight <= 0.0) {
			continue;
		}
		for (int k = 0; k < DDGI_DIRS; k++) {
			vec3 irradiance = textureLod(sampler2D(ddgi_irradiance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, p_dirs[k], ddgi.atlas.x, ddgi.atlas.z, ddgi.atlas_inv_size.xy), 0.0).rgb;
			sum[k] += sqrt(max(irradiance, vec3(0.0))) * weight;
		}
		weight_sum += weight;
	}
	if (weight_sum <= 0.0) {
		return vec4(0.0, 0.0, 0.0, untraced ? 0.0 : -volume_weight);
	}
	for (int k = 0; k < DDGI_DIRS; k++) {
		vec3 result = sum[k] / weight_sum;
		r_irradiance[k] = result * result;
	}
	return vec4(0.0, 0.0, 0.0, volume_weight);
}

// ddgi_sample_irradiance() for DDGI_DIRS directions. Returns the coverage.
float ddgi_sample_irradiance_dirs(vec3 p_pos, vec3 p_normal, vec3 p_view, vec3 p_dirs[DDGI_DIRS], out vec3 r_irradiance[DDGI_DIRS]) {
	vec3 sum[DDGI_DIRS];
	for (int k = 0; k < DDGI_DIRS; k++) {
		sum[k] = vec3(0.0);
	}
	float remaining = 1.0;
	for (uint v = 0u; v < ddgi.counts.x && v < uint(DDGI_MAX_VOLUMES); v++) {
		vec3 e[DDGI_DIRS];
		vec4 s = ddgi_sample_volume_dirs(v, p_pos, p_normal, p_view, p_dirs, e);
		if (s.a == 0.0) {
			continue;
		}
		float weight = abs(s.a);
		for (int k = 0; k < DDGI_DIRS; k++) {
			sum[k] += e[k] * weight * remaining;
		}
		remaining *= 1.0 - weight;
		if (remaining < 0.001) {
			break;
		}
	}
	float coverage = 1.0 - remaining;
	for (int k = 0; k < DDGI_DIRS; k++) {
		r_irradiance[k] = coverage > 0.0 ? sum[k] / coverage : vec3(0.0);
	}
	return coverage;
}

// Mean radiance arriving at a point in the air (no surface, so no normal),
// for volumetric fog: the average of the up and down irradiance of the
// probes around it, with the same visibility test and the same rules for
// unusable probes as ddgi_sample_volume().
vec4 ddgi_sample_volume_isotropic(uint p_volume, vec3 p_pos) {
	DDGIVolume vol = ddgi.volumes[p_volume];
	vec3 local = ddgi_xform(vol.world_to_local, p_pos);
	float volume_weight = ddgi_volume_weight(vol, local);
	if (volume_weight <= 0.0) {
		return vec4(0.0);
	}

	vec3 g = local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
	ivec3 base = clamp(ivec3(floor(g)), ivec3(0), vol.grid.xyz - 2);
	vec3 alpha = clamp(g - vec3(base), vec3(0.0), vec3(1.0));

	vec3 sum = vec3(0.0);
	float weight_sum = 0.0;
	bool untraced = false;

	for (uint i = 0u; i < 8u; i++) {
		ivec3 offs = ivec3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
		ivec3 logical = base + offs;
		uint probe = ddgi_probe_index(vol, logical);
		uint state = ddgi_probes[probe].state;
		// Scrolled-in probes hold the light of the probes that scrolled out (the
		// far side of the grid) until traced: let a coarser cascade cover them.
		if (state == DDGI_PROBE_NEW || state == DDGI_PROBE_SCROLLED) {
			untraced = true;
			continue;
		}
		if (state == DDGI_PROBE_INSIDE) {
			continue;
		}

		vec3 to_probe = ddgi_probe_local_position(vol, logical) + ddgi_probes[probe].offset - local;
		float dist = length(to_probe);
		float weight = 1.0;
		if (dist > 0.0) {
			vec3 world_dir_from_probe = -ddgi_xform_dir(vol.local_to_world, to_probe / dist);
			vec2 moments = textureLod(sampler2D(ddgi_distance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, world_dir_from_probe, ddgi.atlas.y, ddgi.atlas.z, ddgi.atlas_inv_size.zw), 0.0).rg;
			if (dist > moments.x) {
				float variance = abs(moments.x * moments.x - moments.y);
				float d = dist - moments.x;
				float chebyshev = variance / (variance + d * d);
				weight = max(chebyshev * chebyshev * chebyshev, 0.0);
			}
		}
		weight = max(weight, 0.000001);
		const float crush_threshold = 0.2;
		if (weight < crush_threshold) {
			weight *= weight * weight / (crush_threshold * crush_threshold);
		}
		vec3 tri = mix(1.0 - alpha, alpha, vec3(offs));
		weight *= tri.x * tri.y * tri.z;
		if (state == DDGI_PROBE_INACTIVE) {
			weight *= 0.05; // As in ddgi_sample_volume().
		}

		vec2 inv_size = ddgi.atlas_inv_size.xy;
		vec3 up = textureLod(sampler2D(ddgi_irradiance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, vec3(0.0, 1.0, 0.0), ddgi.atlas.x, ddgi.atlas.z, inv_size), 0.0).rgb;
		vec3 down = textureLod(sampler2D(ddgi_irradiance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, vec3(0.0, -1.0, 0.0), ddgi.atlas.x, ddgi.atlas.z, inv_size), 0.0).rgb;
		sum += sqrt(max((up + down) * 0.5, vec3(0.0))) * weight;
		weight_sum += weight;
	}

	if (weight_sum <= 0.0) {
		return vec4(0.0, 0.0, 0.0, untraced ? 0.0 : -volume_weight);
	}
	vec3 result = sum / weight_sum;
	return vec4(result * result, volume_weight);
}

// ddgi_sample_volume_isotropic() over all volumes, blended as in
// ddgi_sample_irradiance(). Returns rgb (normalized) and the coverage in a.
vec4 ddgi_sample_fog_radiance(vec3 p_pos) {
	vec3 sum = vec3(0.0);
	float remaining = 1.0;
	for (uint v = 0u; v < ddgi.counts.x && v < uint(DDGI_MAX_VOLUMES); v++) {
		vec4 s = ddgi_sample_volume_isotropic(v, p_pos);
		if (s.a == 0.0) {
			continue;
		}
		float weight = abs(s.a);
		sum += s.rgb * weight * remaining;
		remaining *= 1.0 - weight;
		if (remaining < 0.001) {
			break;
		}
	}
	float coverage = 1.0 - remaining;
	return coverage > 0.0 ? vec4(sum / coverage, coverage) : vec4(0.0);
}

#ifdef DDGI_MARK_SAMPLED
// Marks the probes a surface at p_pos samples (the 8 around the biased
// lookup position, in every volume until one covers it fully), with the
// frame p_frame, for the scheduler.
void ddgi_mark_sampled_probes(vec3 p_pos, vec3 p_normal, vec3 p_view, uint p_frame) {
	float remaining = 1.0;
	for (uint v = 0u; v < ddgi.counts.x && v < uint(DDGI_MAX_VOLUMES); v++) {
		DDGIVolume vol = ddgi.volumes[v];
		float volume_weight = ddgi_volume_weight(vol, ddgi_xform(vol.world_to_local, p_pos));
		if (volume_weight <= 0.0) {
			continue;
		}
		vec3 local = ddgi_xform(vol.world_to_local, p_pos + p_normal * vol.spacing.y + p_view * vol.spacing.z);
		vec3 g = local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
		ivec3 base = clamp(ivec3(floor(g)), ivec3(0), vol.grid.xyz - 2);
		for (uint i = 0u; i < 8u; i++) {
			uint probe = ddgi_probe_index(vol, base + ivec3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u));
			if (ddgi_sampled_frame[probe] != p_frame) {
				ddgi_sampled_frame[probe] = p_frame;
			}
		}
		remaining *= 1.0 - volume_weight;
		if (remaining < 0.001) {
			break;
		}
	}
}
#endif

// Index of the finest volume that covers a point, or -1.
int ddgi_volume_at(vec3 p_pos) {
	for (uint v = 0u; v < ddgi.counts.x && v < uint(DDGI_MAX_VOLUMES); v++) {
		DDGIVolume vol = ddgi.volumes[v];
		if (ddgi_volume_weight(vol, ddgi_xform(vol.world_to_local, p_pos)) > 0.0) {
			return int(v);
		}
	}
	return -1;
}

#endif // DDGI_SAMPLING
