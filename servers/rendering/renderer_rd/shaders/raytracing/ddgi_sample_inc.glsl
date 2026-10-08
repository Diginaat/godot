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

#ifdef DDGI_SAMPLING

// Fraction of the volume's weight at a point: 1 inside, fading to 0 over the
// edge blend width at the borders of the probe grid.
float ddgi_volume_weight(DDGIVolume vol, vec3 p_local) {
	vec3 g = p_local / vol.spacing.x + (vec3(vol.grid.xyz) - 1.0) * 0.5;
	vec3 edge = min(g, vec3(vol.grid.xyz) - 1.0 - g);
	float d = min(edge.x, min(edge.y, edge.z));
	return clamp(d / max(vol.params.w, 0.001), 0.0, 1.0);
}

// Irradiance (cosine weighted mean radiance) at world position p_pos with
// normal p_normal, seen from direction p_view (unit vector toward the viewer),
// from one volume. Returns rgb and the volume weight in a.
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
	uint dist_texels = ddgi.atlas.y;
	uint per_row = ddgi.atlas.z;

	vec3 sum = vec3(0.0);
	float weight_sum = 0.0;

	for (uint i = 0u; i < 8u; i++) {
		ivec3 offs = ivec3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
		ivec3 logical = base + offs;
		uint probe = ddgi_probe_index(vol, logical);
		DDGIProbe pd = ddgi_probes[probe];
		if (pd.state == DDGI_PROBE_NEW || pd.state == DDGI_PROBE_INSIDE) {
			continue;
		}

		vec3 probe_local = ddgi_probe_local_position(vol, logical) + pd.offset;
		vec3 to_probe = probe_local - local;
		float dist = length(to_probe);
		vec3 dir = dist > 0.0 ? to_probe / dist : local_normal;

		vec3 tri = mix(1.0 - alpha, alpha, vec3(offs));
		float trilinear = tri.x * tri.y * tri.z;

		// Smooth backface: probes behind the surface count less, but never zero,
		// so thin geometry doesn't go black.
		float wrap = (dot(dir, local_normal) + 1.0) * 0.5;
		float weight = wrap * wrap + 0.2;

		// Chebyshev visibility from the probe's distance moments, looked up in
		// the direction from the probe to the shading point.
		vec3 world_dir_from_probe = -ddgi_xform_dir(vol.local_to_world, dir);
		vec2 moments = textureLod(sampler2D(ddgi_distance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, world_dir_from_probe, dist_texels, per_row, ddgi.atlas_inv_size.zw), 0.0).rg;
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

		vec3 irradiance = textureLod(sampler2D(ddgi_irradiance_atlas, DDGI_SAMPLER), ddgi_atlas_uv(probe, p_normal, irr_texels, per_row, ddgi.atlas_inv_size.xy), 0.0).rgb;
		// Blend in a perceptual (square root) space: smoother transitions
		// between bright and dark probes.
		sum += sqrt(max(irradiance, vec3(0.0))) * weight;
		weight_sum += weight;
	}

	if (weight_sum <= 0.0) {
		return vec4(0.0);
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
		if (s.a <= 0.0) {
			continue;
		}
		sum += s.rgb * s.a * remaining;
		remaining *= 1.0 - s.a;
		if (remaining < 0.001) {
			break;
		}
	}
	float coverage = 1.0 - remaining;
	return coverage > 0.0 ? vec4(sum / coverage, coverage) : vec4(0.0);
}

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
