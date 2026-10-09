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
//   MODE_SMOOTH: one thread per irradiance atlas texel. Eases the display
//     irradiance (what surfaces sample) toward the probes' irradiance.

#ifdef MODE_SCHEDULE
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#elif defined(MODE_SMOOTH)
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
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
	uint ddgi_update_count;
	uint ddgi_update_pad0;
	uint ddgi_update_pad1;
	uint ddgi_update_pad2;
	uint ddgi_update_probes[];
};

layout(set = 0, binding = 3, rgba16f) uniform restrict readonly image2D ddgi_ray_data;

// Persistent across frames. The scheduler multiplies every update rate by
// rate_scale; the relocate pass raises it while the per-frame budget isn't
// filled and lowers it when the budget overflows, so the budget gets used.
layout(set = 0, binding = 5, std430) restrict buffer DDGIStats {
	float rate_scale;
	uint traced_last_frame;
	uint stats_pad0;
	uint stats_pad1;
}
ddgi_stats;

#ifdef MODE_BLEND_IRRADIANCE
layout(set = 0, binding = 4, rgba16f) uniform restrict coherent image2D ddgi_atlas;
// The first update of a probe also replaces what surfaces see, instead of
// easing in from stale or empty texels.
layout(set = 0, binding = 6, rgba16f) uniform restrict writeonly image2D ddgi_display;
#endif
#ifdef MODE_SMOOTH
layout(set = 0, binding = 4, rgba16f) uniform restrict readonly image2D ddgi_atlas;
layout(set = 0, binding = 6, rgba16f) uniform restrict image2D ddgi_display;
#endif
#ifdef MODE_BLEND_DISTANCE
layout(set = 0, binding = 4, rg16f) uniform restrict coherent image2D ddgi_atlas;
#endif

layout(push_constant, std430) uniform Params {
	uint total_probes;
	uint schedule_new_only;
	uint realtime; // 1: every probe in turn (see below).
	uint realtime_offset; // First probe of this frame's chunk.
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
	bool full_reset = reset.w != 0;
	bool scroll_reset = false;
	for (int a = 0; a < 3 && !scroll_reset; a++) {
		int d = reset[a];
		if ((d > 0 && logical[a] >= vol.grid[a] - d) || (d < 0 && logical[a] < -d)) {
			scroll_reset = true;
		}
	}
	if (full_reset) {
		pd.offset = vec3(0.0);
		pd.state = DDGI_PROBE_NEW;
		pd.urgency = 0.0;
		pd.variability = 1.0;
		pd.last_update_frame = 0u;
		pd.pending_change = 0.0;
	} else if (scroll_reset) {
		// Keep the atlas texels as a fallback until the first trace. They belong
		// to the probe that scrolled out at the far side, so they are stale, but
		// stale lighting is less visible than sampling no probe at all (black
		// flicker) while the camera moves.
		pd.offset = vec3(0.0);
		pd.state = DDGI_PROBE_SCROLLED;
		pd.urgency = max(pd.urgency, 1.0);
		pd.variability = 1.0;
		pd.last_update_frame = 0u;
		pd.pending_change = 0.0;
	}

	// Realtime updates: no credit, no priorities. Each frame traces the next
	// chunk of probes in index order (a slab of the grid), every probe once
	// per round; with the capacity at or above the probe count, all of them
	// every frame. All probes follow a light change together, at the same
	// rate, instead of one by one in their own turn.
	if (params.realtime != 0u) {
		if (params.schedule_new_only != 0u) {
			uint rel = (probe + params.total_probes - params.realtime_offset) % params.total_probes;
			if (rel < ddgi.counts.w) {
				ddgi_update_probes[rel] = probe;
				pd.urgency = 0.0;
				pd.last_update_frame = ddgi.atlas.w;
			}
			ddgi_probes[probe] = pd;
		}
		return;
	}

	bool new_pass = params.schedule_new_only != 0u;
	// A pending change is confirmed (or dropped) by the next update: don't
	// make the light change wait for the probe's usual turn. (Inside probes
	// are never blended, so their pending change would never clear.)
	bool pending = pd.state != DDGI_PROBE_INSIDE && abs(pd.pending_change) > 0.05;
	bool priority_probe = pd.state == DDGI_PROBE_NEW || pd.last_update_frame == 0u || pending;
	if (new_pass) {
		if (!priority_probe) {
			ddgi_probes[probe] = pd;
			return;
		}
	} else if (priority_probe) {
		// New/full-reset probes, scrolled probes and probes with a pending
		// change were offered the budget first.
		// If the budget was exhausted by them, keep them due for the next frame
		// instead of letting older probes jump the queue.
		ddgi_probes[probe] = pd;
		return;
	}

	// Update rate: the base rate spreads the per-frame budget over all
	// probes; each probe then gets more or less of it.
	float rate = ddgi.schedule.x * vol.params.z * ddgi_stats.rate_scale;
	if (new_pass) {
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

// The probe's rays, loaded once per workgroup instead of once per texel.
#define MAX_RAYS 512
shared vec4 shared_rays[MAX_RAYS]; // Irradiance: rgb radiance, a 1 if usable. Distance: x d, y d*d, z 1 if usable.
shared vec3 shared_dirs[MAX_RAYS];

#ifdef MODE_BLEND_IRRADIANCE
#define TILE_TEXELS ddgi.atlas.x
// Up to 16x16 texels per tile.
shared vec3 shared_result[256];
shared vec3 shared_previous[256];
shared float shared_noise[256]; // Relative noise of each texel, remembered over updates.
shared vec3 shared_sum_result[64];
shared vec3 shared_sum_previous[64];
shared vec3 shared_ray_stats[64]; // Luminance sum, squared sum and count of the usable rays.
shared float shared_hysteresis;
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
	// New and scrolled-in probes have no data of their own yet: replace it.
	bool first_update = pd.state == DDGI_PROBE_NEW || pd.state == DDGI_PROBE_SCROLLED;
	float hysteresis = first_update ? 0.0 : vol.params.y;
	float max_distance = vol.spacing.w;

	uint local_index = gl_LocalInvocationIndex;
#ifdef MODE_BLEND_IRRADIANCE
	vec3 sum_result = vec3(0.0);
	vec3 sum_previous = vec3(0.0);
	vec3 ray_stats = vec3(0.0);
#endif

	uint ray_count = min(ray_end - ray_begin, uint(MAX_RAYS));
	for (uint i = local_index; i < ray_count; i += 64u) {
		uint r = ray_begin + i;
		vec4 ray = imageLoad(ddgi_ray_data, ivec2(r, slot));
		shared_dirs[i] = ddgi_probe_ray_direction(r);
#ifdef MODE_BLEND_IRRADIANCE
		// Back faces (the inside of geometry) add no light; a single NaN would
		// stay in the probe forever (hysteresis).
		bool usable = ray.a >= 0.0 && !any(isnan(ray.rgb)) && !any(isinf(ray.rgb));
		// Clamp single bright rays (fireflies) without changing the hue.
		vec3 radiance = ray.rgb;
		float lum = ddgi_luminance(radiance);
		if (lum > ddgi.schedule.z) {
			radiance *= ddgi.schedule.z / lum;
		}
		// a: luminance, or -1 for rays that add nothing.
		float l = ddgi_luminance(radiance);
		shared_rays[i] = usable ? vec4(radiance, l) : vec4(0.0, 0.0, 0.0, -1.0);
		if (usable) {
			ray_stats += vec3(l, l * l, 1.0);
		}
#else
		float d = ray.a;
		bool usable = !isnan(d) && !isinf(d);
		// Back faces count as very close: points past them are occluded.
		d = d < 0.0 ? -d * 0.2 : min(d, max_distance);
		shared_rays[i] = usable ? vec4(d, d * d, 1.0, 0.0) : vec4(0.0);
#endif
	}
	barrier();

	for (int ty = int(gl_LocalInvocationID.y); ty < texels; ty += 8) {
		for (int tx = int(gl_LocalInvocationID.x); tx < texels; tx += 8) {
			ivec2 texel = ivec2(tx, ty);
			vec3 texel_dir = ddgi_texel_direction(texel, uint(texels));

#ifdef MODE_BLEND_IRRADIANCE
			vec3 sum = vec3(0.0);
			float weight_sum = 0.0;
			float lum_sq_sum = 0.0;
			float weight_sq_sum = 0.0;
			for (uint i = 0u; i < ray_count; i++) {
				vec4 ray = shared_rays[i];
				float w = ray.a >= 0.0 ? max(0.0, dot(texel_dir, shared_dirs[i])) : 0.0;
				sum += ray.rgb * w;
				weight_sum += w;
				lum_sq_sum += ray.a * ray.a * w;
				weight_sq_sum += w * w;
			}
			// Standard error of this texel's mean, relative to it: high when a
			// few rays carry most of its light (a window, a small lamp).
			float texel_noise = 0.0;
			if (weight_sum > 0.0) {
				float mean_l = ddgi_luminance(sum) / weight_sum;
				float variance = max(lum_sq_sum / weight_sum - mean_l * mean_l, 0.0);
				float effective_rays = weight_sum * weight_sum / max(weight_sq_sum, 1e-8);
				texel_noise = sqrt(variance / effective_rays) / max(mean_l, 1e-4);
			}
			vec4 previous_texel = imageLoad(ddgi_atlas, origin + texel);
			vec3 previous = previous_texel.rgb;
			if (any(isnan(previous_texel))) {
				previous = vec3(0.0);
				previous_texel.a = 0.0;
			}
			// The estimate is itself noisy: in an update where none of the few
			// rays toward the bright source hit it, it reads low just when the
			// value is most off. Alpha keeps a slowly decaying maximum instead.
			shared_noise[ty * texels + tx] = first_update ? texel_noise : max(texel_noise, previous_texel.a * 0.9);
			vec3 result = weight_sum > 0.0 ? sum / weight_sum : previous;
			shared_result[ty * texels + tx] = result;
			shared_previous[ty * texels + tx] = previous;
			sum_result += result;
			sum_previous += previous;
#else
			vec2 sum = vec2(0.0);
			float weight_sum = 0.0;
			for (uint i = 0u; i < ray_count; i++) {
				vec4 ray = shared_rays[i];
				float c = max(0.0, dot(texel_dir, shared_dirs[i]));
				// A sharp lobe (cos^50): distance varies faster than light.
				float c2 = c * c;
				float c4 = c2 * c2;
				float c8 = c4 * c4;
				float c16 = c8 * c8;
				float c32 = c16 * c16;
				float w = c32 * c16 * c2 * ray.z;
				sum += ray.xy * w;
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
	shared_sum_result[local_index] = sum_result;
	shared_sum_previous[local_index] = sum_previous;
	shared_ray_stats[local_index] = ray_stats;
	barrier();

	// How much the whole probe changed: the tile average is far less noisy
	// than single texels, so noise doesn't count as a lighting change.
	if (local_index == 0u) {
		vec3 total_result = vec3(0.0);
		vec3 total_previous = vec3(0.0);
		vec3 stats = vec3(0.0);
		for (uint i = 0u; i < 64u; i++) {
			total_result += shared_sum_result[i];
			total_previous += shared_sum_previous[i];
			stats += shared_ray_stats[i];
		}
		float floor_value = 0.02 * float(texels * texels);
		float change = length(total_result - total_previous) / max(length(total_previous), floor_value);
		// Part of every change is ray noise: in a dark room lit by a small
		// bright patch (sunlight through a window), a few rays carry most of
		// the light, and the tile average jumps by more than the old
		// threshold from one update to the next with the light unchanged.
		// Counted as a lighting change, that noise lowered the hysteresis and
		// the indirect light flickered. Only the change above about two
		// standard errors of this update's mean counts.
		float n = max(stats.z, 1.0);
		float mean_lum = stats.x / n;
		float std_error = sqrt(max(stats.y / n - mean_lum * mean_lum, 0.0) / n);
		float tile_lum = max(ddgi_luminance(total_previous), ddgi_luminance(total_result)) / float(texels * texels);
		float noise = 2.0 * std_error / max(max(tile_lum, mean_lum), floor_value / float(texels * texels));
		float signal = max(change - noise, 0.0);
		// A few rays that hit something much brighter than the rest (a
		// sunlit patch seen from a dark room) still pass that test now and
		// then. They don't repeat; a real light change does. So the change
		// only counts when the previous update changed the same way too; the
		// scheduler traces a probe with a pending change again next frame.
		float direction = ddgi_luminance(total_result) >= ddgi_luminance(total_previous) ? 1.0 : -1.0;
		float confirmed = pd.pending_change * direction > 0.0 ? min(signal, abs(pd.pending_change)) : 0.0;
		ddgi_probes[probe].pending_change = first_update ? 0.0 : signal * direction;
		// Adapt faster when the light changed a lot (lights switched, doors
		// opened), so the GI doesn't lag behind. 1 when it didn't. (Reacting
		// to small confirmed changes too was tried: with skewed noise, two
		// small dips in a row are common and two spikes rare, so it pulled
		// rooms lit by a small source darker.)
		shared_hysteresis = first_update ? 0.0 : clamp(1.0 - (confirmed - 0.1) * 1.5, 0.4, 1.0);
		// Moving average of how much this probe's light changes per update.
		ddgi_probes[probe].variability = first_update ? 1.0 : mix(pd.variability, min(signal, 4.0), 0.3);
	}
	barrier();

	float change_factor = shared_hysteresis;
	for (int ty = int(gl_LocalInvocationID.y); ty < texels; ty += 8) {
		for (int tx = int(gl_LocalInvocationID.x); tx < texels; tx += 8) {
			int i = ty * texels + tx;
			// Noisy texels blend each update in with down to half the weight:
			// less flicker, at the cost of a slower response to light changes
			// too small to be told from the noise.
			float steady = hysteresis > 0.0 ? mix(hysteresis, 1.0 - (1.0 - hysteresis) * 0.5, clamp(shared_noise[i] * 2.0, 0.0, 1.0)) : 0.0;
			float h = steady * change_factor;
			vec4 value = vec4(mix(shared_result[i], shared_previous[i], h), min(shared_noise[i], 8.0));
			imageStore(ddgi_atlas, origin + ivec2(tx, ty), value);
			if (first_update) {
				imageStore(ddgi_display, origin + ivec2(tx, ty), value);
			}
		}
	}
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
		vec4 border = imageLoad(ddgi_atlas, origin + src);
		imageStore(ddgi_atlas, origin + b, border);
#ifdef MODE_BLEND_IRRADIANCE
		if (first_update) {
			imageStore(ddgi_display, origin + b, border);
		}
#endif
	}

}

#endif // MODE_BLEND_IRRADIANCE || MODE_BLEND_DISTANCE

#ifdef MODE_SMOOTH

// Probes update at different times, each with a step. Surfaces sample a copy
// that closes a fixed share of its gap every frame, so a light change (a
// storm rolling in, a light switched) fades in evenly over the whole scene
// instead of probe by probe. Borders are eased like the interior, so they
// stay copies of it.
void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, imageSize(ddgi_display)))) {
		return;
	}
	vec4 target = imageLoad(ddgi_atlas, texel);
	vec4 shown = imageLoad(ddgi_display, texel);
	if (any(isnan(shown)) || any(isinf(shown))) {
		shown = target;
	}
	imageStore(ddgi_display, texel, mix(shown, target, ddgi.smoothing.x));
}

#endif // MODE_SMOOTH

#ifdef MODE_RELOCATE_CLASSIFY

void main() {
	uint slot = gl_GlobalInvocationID.x;
	if (slot == 0u) {
		// Budget feedback for the next frame's scheduler.
		uint traced = ddgi_traced_count();
		float scale = ddgi_stats.rate_scale;
		if (traced >= ddgi.counts.w) {
			scale *= 0.9;
		} else if (float(traced) < 0.9 * float(ddgi.counts.w)) {
			scale *= 1.15;
		}
		ddgi_stats.rate_scale = clamp(scale, 1.0, 64.0);
		ddgi_stats.traced_last_frame = traced;
	}
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
