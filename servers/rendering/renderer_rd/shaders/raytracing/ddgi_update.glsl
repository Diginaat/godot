#[compute]

#version 450

#VERSION_DEFINES

// DDGI probe update passes. One shader, one variant per pass:
//   MODE_SCHEDULE: one thread per probe. Resets probes uncovered by scrolling
//     and picks the probes to trace this frame (credit-based scheduler).
//   MODE_BLEND_IRRADIANCE, MODE_BLEND_DISTANCE: one workgroup per traced
//     probe. Blends the new rays into the probe's octahedral tile, then
//     fills the tile border for bilinear filtering.
//   MODE_RELOCATE_CLASSIFY: one thread per traced probe. Moves probes out of
//     geometry and marks probes as active, inactive or inside.

#ifdef MODE_SCHEDULE
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#elif defined(MODE_RELOCATE_CLASSIFY)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

#include "../oct_inc.glsl"
#include "ddgi_inc.glsl"

layout(set = 0, binding = 0, std140) uniform DDGIUniforms {
	DDGIDataBlock ddgi;
};

layout(set = 0, binding = 1, std430) restrict buffer DDGIProbes {
	DDGIProbe ddgi_probes[];
};

layout(set = 0, binding = 2, std430) restrict buffer DDGIUpdateList {
	uint ddgi_update_count; // Also the x group count of the blend dispatches.
	uint ddgi_update_groups_y;
	uint ddgi_update_groups_z;
	uint ddgi_update_pad;
	uint ddgi_update_probes[];
};

layout(set = 0, binding = 3, rgba16f) uniform restrict readonly image2D ddgi_ray_data;

#ifdef MODE_BLEND_IRRADIANCE
layout(set = 0, binding = 4, rgba16f) uniform restrict coherent image2D ddgi_atlas;
#endif
#ifdef MODE_BLEND_DISTANCE
layout(set = 0, binding = 4, rg16f) uniform restrict coherent image2D ddgi_atlas;
#endif

layout(push_constant, std430) uniform Params {
	uint total_probes;
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

#include "ddgi_sample_inc.glsl"

uint ddgi_find_volume(uint p_probe) {
	for (uint v = 0u; v < ddgi.counts.x; v++) {
		ivec4 grid = ddgi.volumes[v].grid;
		if (int(p_probe) >= grid.w && int(p_probe) < grid.w + grid.x * grid.y * grid.z) {
			return v;
		}
	}
	return 0u;
}

uint ddgi_traced_count() {
	return min(ddgi_update_count, ddgi.counts.w);
}

#ifdef MODE_SCHEDULE

void main() {
	uint probe = gl_GlobalInvocationID.x;
	if (probe >= params.total_probes) {
		return;
	}
	uint volume = ddgi_find_volume(probe);
	DDGIVolume vol = ddgi.volumes[volume];
	DDGIProbe pd = ddgi_probes[probe];
	ivec3 logical = ddgi_probe_logical(vol, probe);

	// Probes that scrolling (or a reset) brought into the volume start over.
	ivec4 reset = ddgi.volume_reset[volume];
	bool reset_probe = reset.w != 0;
	for (int a = 0; a < 3 && !reset_probe; a++) {
		int d = reset[a];
		if ((d > 0 && logical[a] >= vol.grid[a] - d) || (d < 0 && logical[a] < -d)) {
			reset_probe = true;
		}
	}
	if (reset_probe) {
		pd.offset = vec3(0.0);
		pd.state = DDGI_PROBE_NEW;
		pd.urgency = 0.0;
		pd.variability = 1.0;
		pd.last_update_frame = 0u;
		pd.luminance = 0.0;
	}

	// Update rate: the base rate spreads the per-frame budget over all
	// probes; each probe then gets more or less of it.
	float rate = ddgi.schedule.x * vol.params.z;
	if (pd.state == DDGI_PROBE_NEW) {
		rate = 1000.0; // As soon as there is room.
	} else if (pd.state == DDGI_PROBE_INACTIVE || pd.state == DDGI_PROBE_INSIDE) {
		rate *= 0.125; // Only to notice changes in classification.
	} else {
		// Probes whose lighting changes get more updates, stable ones fewer.
		rate *= mix(0.5, 3.0, clamp(pd.variability * 4.0, 0.0, 1.0));

		// Probes in view (and close to the camera) matter most, but probes
		// off screen still update so turning around doesn't show stale light.
		vec3 world_pos = ddgi_probe_world_position(vol, logical, pd.offset);
		vec4 clip = ddgi.camera_view_projection * vec4(world_pos, 1.0);
		bool in_view = clip.w > 0.0 && all(lessThanEqual(abs(clip.xy), vec2(clip.w * 1.2)));
		rate *= in_view ? 2.0 : 0.6;
	}

	pd.urgency += rate;
	if (pd.urgency >= 1.0) {
		uint slot = atomicAdd(ddgi_update_count, 1u);
		if (slot < ddgi.counts.w) {
			ddgi_update_probes[slot] = probe;
			pd.urgency = 0.0;
			pd.last_update_frame = ddgi.atlas.w;
		} else {
			// No room this frame: give the slot back and stay due.
			atomicAdd(ddgi_update_count, uint(-1));
		}
	}
	ddgi_probes[probe] = pd;
}

#endif // MODE_SCHEDULE

#if defined(MODE_BLEND_IRRADIANCE) || defined(MODE_BLEND_DISTANCE)

#ifdef MODE_BLEND_IRRADIANCE
#define TILE_TEXELS ddgi.atlas.x
shared float shared_change[64];
#else
#define TILE_TEXELS ddgi.atlas.y
#endif

void main() {
	uint slot = gl_WorkGroupID.x;
	if (slot >= ddgi_traced_count()) {
		return;
	}
	uint probe = ddgi_update_probes[slot];
	DDGIProbe pd = ddgi_probes[probe];
	if (pd.state == DDGI_PROBE_INSIDE) {
		return; // Never sampled; keep whatever it had.
	}
	DDGIVolume vol = ddgi.volumes[ddgi_find_volume(probe)];

	// Inactive probes only traced the fixed rays; active ones blend only the
	// randomly rotated rays (the fixed ones always point the same way).
	bool fixed_only = pd.state == DDGI_PROBE_INACTIVE;
	uint ray_begin = fixed_only ? 0u : ddgi.counts.z;
	uint ray_end = fixed_only ? ddgi.counts.z : ddgi.counts.y;

	int texels = int(TILE_TEXELS);
	ivec2 origin = ddgi_tile_origin(probe, uint(texels), ddgi.atlas.z);
	float hysteresis = pd.state == DDGI_PROBE_NEW ? 0.0 : vol.params.y;
	float max_distance = vol.spacing.w;

	uint local_index = gl_LocalInvocationIndex;
	float change_sum = 0.0;

	for (int ty = int(gl_LocalInvocationID.y); ty < texels; ty += 8) {
		for (int tx = int(gl_LocalInvocationID.x); tx < texels; tx += 8) {
			ivec2 texel = ivec2(tx, ty);
			vec3 texel_dir = ddgi_texel_direction(texel, uint(texels));

#ifdef MODE_BLEND_IRRADIANCE
			vec3 sum = vec3(0.0);
			float weight_sum = 0.0;
			float max_radiance = ddgi.schedule.z;
			for (uint r = ray_begin; r < ray_end; r++) {
				vec4 ray = imageLoad(ddgi_ray_data, ivec2(r, slot));
				if (ray.a < 0.0) {
					continue; // Back face: the inside of geometry adds no light.
				}
				// A single NaN would stay in the probe forever (hysteresis).
				if (any(isnan(ray.rgb)) || any(isinf(ray.rgb))) {
					continue;
				}
				float w = max(0.0, dot(texel_dir, ddgi_probe_ray_direction(r)));
				// Clamp single bright rays (fireflies) without changing the hue.
				vec3 radiance = ray.rgb;
				float lum = ddgi_luminance(radiance);
				if (lum > max_radiance) {
					radiance *= max_radiance / lum;
				}
				sum += radiance * w;
				weight_sum += w;
			}
			vec4 previous = imageLoad(ddgi_atlas, origin + texel);
			if (any(isnan(previous.rgb))) {
				previous.rgb = vec3(0.0);
			}
			vec3 result = weight_sum > 0.0 ? sum / weight_sum : previous.rgb;

			// Adapt faster when the lighting changed a lot (lights switched,
			// doors opened), so the GI doesn't lag behind.
			float change = length(result - previous.rgb) / max(length(previous.rgb), 0.05);
			float h = hysteresis;
			if (change > 0.5) {
				h *= 0.7;
			}
			if (pd.state == DDGI_PROBE_NEW) {
				change = 1.0;
			}
			change_sum += min(change, 4.0);
			imageStore(ddgi_atlas, origin + texel, vec4(mix(result, previous.rgb, h), 1.0));
#else
			vec2 sum = vec2(0.0);
			float weight_sum = 0.0;
			for (uint r = ray_begin; r < ray_end; r++) {
				float d = imageLoad(ddgi_ray_data, ivec2(r, slot)).a;
				if (isnan(d) || isinf(d)) {
					continue;
				}
				// Back faces count as very close: points past them are occluded.
				d = d < 0.0 ? -d * 0.2 : min(d, max_distance);
				float w = pow(max(0.0, dot(texel_dir, ddgi_probe_ray_direction(r))), 50.0);
				sum += vec2(d, d * d) * w;
				weight_sum += w;
			}
			vec2 previous = imageLoad(ddgi_atlas, origin + texel).rg;
			if (any(isnan(previous))) {
				previous = vec2(max_distance, max_distance * max_distance);
			}
			vec2 result = weight_sum > 0.0 ? sum / weight_sum : previous;
			imageStore(ddgi_atlas, origin + texel, vec4(mix(result, previous, hysteresis), 0.0, 0.0));
#endif
		}
	}

#ifdef MODE_BLEND_IRRADIANCE
	shared_change[local_index] = change_sum;
#endif

	memoryBarrierImage();
	barrier();

	// Border texels copy the interior (octahedral wrap) so bilinear filtering
	// at the tile edge reads the right neighbors.
	int border_count = 4 * texels + 4;
	for (int i = int(local_index); i < border_count; i += 64) {
		ivec2 b;
		if (i < texels) {
			b = ivec2(i, -1);
		} else if (i < 2 * texels) {
			b = ivec2(i - texels, texels);
		} else if (i < 3 * texels) {
			b = ivec2(-1, i - 2 * texels);
		} else if (i < 4 * texels) {
			b = ivec2(texels, i - 3 * texels);
		} else {
			int c = i - 4 * texels;
			b = ivec2((c & 1) != 0 ? texels : -1, (c & 2) != 0 ? texels : -1);
		}
		ivec2 src = ddgi_border_source(b, texels);
		imageStore(ddgi_atlas, origin + b, imageLoad(ddgi_atlas, origin + src));
	}

#ifdef MODE_BLEND_IRRADIANCE
	if (local_index == 0u) {
		float total = 0.0;
		for (uint i = 0u; i < 64u; i++) {
			total += shared_change[i];
		}
		float mean_change = total / float(texels * texels);
		// Moving average of how much this probe's light changes per update.
		pd.variability = pd.state == DDGI_PROBE_NEW ? 1.0 : mix(pd.variability, mean_change, 0.3);
		ddgi_probes[probe].variability = pd.variability;
	}
#endif
}

#endif // MODE_BLEND_IRRADIANCE || MODE_BLEND_DISTANCE

#ifdef MODE_RELOCATE_CLASSIFY

void main() {
	uint slot = gl_GlobalInvocationID.x;
	if (slot >= ddgi_traced_count()) {
		return;
	}
	uint probe = ddgi_update_probes[slot];
	DDGIProbe pd = ddgi_probes[probe];
	DDGIVolume vol = ddgi.volumes[ddgi_find_volume(probe)];
	float spacing = vol.spacing.x;
	uint fixed_rays = ddgi.counts.z;

	uint backfaces = 0u;
	float closest_back = 1e20;
	vec3 closest_back_dir = vec3(0.0);
	float closest_front = 1e20;
	vec3 closest_front_dir = vec3(0.0);
	bool surface_nearby = false;

	for (uint r = 0u; r < fixed_rays; r++) {
		float d = imageLoad(ddgi_ray_data, ivec2(r, slot)).a;
		vec3 dir = ddgi_xform_dir(vol.world_to_local, ddgi_probe_ray_direction(r));
		float dist = abs(d);
		if (d < 0.0) {
			backfaces++;
			if (dist < closest_back) {
				closest_back = dist;
				closest_back_dir = dir;
			}
		} else if (dist < closest_front) {
			closest_front = dist;
			closest_front_dir = dir;
		}
		// A surface inside the cells this probe contributes to (one spacing
		// on every axis) means the probe lights something.
		if (d < DDGI_MISS_DISTANCE && all(lessThanEqual(abs(dir * dist), vec3(spacing)))) {
			surface_nearby = true;
		}
	}

	float backface_ratio = float(backfaces) / float(max(fixed_rays, 1u));
	bool inside = backface_ratio > 0.25;

	if ((vol.scroll.w & DDGI_VOLUME_FLAG_RELOCATION) != 0) {
		vec3 offset = pd.offset;
		float min_front = 0.2 * spacing;
		if (inside) {
			// Step through the closest back face to get out of the geometry.
			offset += closest_back_dir * (closest_back + min_front);
		} else if (closest_front < min_front) {
			// Too close to a surface: the visibility test gets unreliable.
			offset -= closest_front_dir * (min_front - closest_front);
		}
		pd.offset = clamp(offset, vec3(-0.45 * spacing), vec3(0.45 * spacing));
	} else {
		pd.offset = vec3(0.0);
	}

	if ((vol.scroll.w & DDGI_VOLUME_FLAG_CLASSIFICATION) != 0) {
		pd.state = inside ? DDGI_PROBE_INSIDE : (surface_nearby ? DDGI_PROBE_ACTIVE : DDGI_PROBE_INACTIVE);
	} else {
		pd.state = DDGI_PROBE_ACTIVE;
	}

	ddgi_probes[probe].offset = pd.offset;
	ddgi_probes[probe].state = pd.state;
}

#endif // MODE_RELOCATE_CLASSIFY
