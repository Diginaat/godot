// Directional shadow receiver masks (shadow_receivers.glsl) and the caster
// test that reads them (gpu_instance_cull.glsl, MODE_SHADOW_BATCH). The
// includer declares `cells` (the masks) and `grids` (their parameters).
//
// A mask has a fine grid over the cascade's shadow map and two coarse ones
// (RECEIVER_COARSE fine cells per side): "all" bounds every receiver, "only"
// holds the receivers too large to mark fine cells. A cell stores the
// farthest light distance of its receivers as receiver_value(), 0 when it
// has none.

#define RECEIVER_GRID_SIZE 64
#define RECEIVER_COARSE 8
#define RECEIVER_COARSE_SIZE (RECEIVER_GRID_SIZE / RECEIVER_COARSE)
#define RECEIVER_COARSE_ALL (RECEIVER_GRID_SIZE * RECEIVER_GRID_SIZE)
#define RECEIVER_COARSE_ONLY (RECEIVER_COARSE_ALL + RECEIVER_COARSE_SIZE * RECEIVER_COARSE_SIZE)
#define RECEIVER_GRID_WORDS (RECEIVER_COARSE_ONLY + RECEIVER_COARSE_SIZE * RECEIVER_COARSE_SIZE)
#define RECEIVER_FINE_LIMIT 64 // Largest footprint, in fine cells, that marks or tests fine cells.
// vec4s per grid in `grids.data`: cell transform (light xy scale, offset),
// three world-to-light rows, then (near clip depth, far clip depth,
// dilation in cells, caster depth margin).
#define RECEIVER_GRID_VEC4S 5

// Positive and ordered as uint (atomicMax); never 0.
uint receiver_value(float p_light_distance) {
	return floatBitsToUint(max(p_light_distance, 0.0) + 1.0);
}

#ifndef RECEIVER_BUILD
// True if an AABB (world space) may cast onto a receiver of the grid: some
// cell of its light-space footprint holds a receiver at least as far from
// the light as the box's nearest point.
bool receivers_reachable(uint p_grid, vec3 p_position, vec3 p_size) {
	uint first = p_grid * RECEIVER_GRID_VEC4S;
	vec4 cell_transform = grids.data[first];
	vec4 ranges = grids.data[first + 4u];
	vec3 half_size = p_size * 0.5;
	vec4 center = vec4(p_position + half_size, 1.0);
	vec3 light_center;
	vec3 light_extent;
	for (uint row = 0u; row < 3u; row++) {
		vec4 r = grids.data[first + 1u + row];
		light_center[row] = dot(r, center);
		light_extent[row] = dot(abs(r.xyz), half_size);
	}
	vec2 a = (light_center.xy - light_extent.xy) * cell_transform.xy + cell_transform.zw;
	vec2 b = (light_center.xy + light_extent.xy) * cell_transform.xy + cell_transform.zw;
	vec2 cell_low = min(a, b);
	vec2 cell_high = max(a, b);
	if (any(isnan(cell_low)) || any(isnan(cell_high)) || any(isinf(cell_low)) || any(isinf(cell_high)) || isnan(light_center.z) || isinf(light_center.z)) {
		return true;
	}
	if (any(greaterThanEqual(cell_low, vec2(RECEIVER_GRID_SIZE))) || any(lessThan(cell_high, vec2(0.0)))) {
		return false; // Draws nothing into the cascade's shadow map.
	}
	ivec2 low = clamp(ivec2(floor(cell_low)), ivec2(0), ivec2(RECEIVER_GRID_SIZE - 1));
	ivec2 high = clamp(ivec2(floor(cell_high)), ivec2(0), ivec2(RECEIVER_GRID_SIZE - 1));
	// The light camera looks along -Z: the point nearest to the light has the largest z.
	uint needed = receiver_value(-(light_center.z + light_extent.z) - ranges.w);
	uint base = p_grid * RECEIVER_GRID_WORDS;
	ivec2 coarse_low = low / RECEIVER_COARSE;
	ivec2 coarse_high = high / RECEIVER_COARSE;
	ivec2 extent = high - low + 1;
	if (extent.x * extent.y > RECEIVER_FINE_LIMIT) {
		for (int y = coarse_low.y; y <= coarse_high.y; y++) {
			for (int x = coarse_low.x; x <= coarse_high.x; x++) {
				if (cells.data[base + RECEIVER_COARSE_ALL + uint(y * RECEIVER_COARSE_SIZE + x)] >= needed) {
					return true;
				}
			}
		}
		return false;
	}
	for (int y = coarse_low.y; y <= coarse_high.y; y++) {
		for (int x = coarse_low.x; x <= coarse_high.x; x++) {
			if (cells.data[base + RECEIVER_COARSE_ONLY + uint(y * RECEIVER_COARSE_SIZE + x)] >= needed) {
				return true;
			}
		}
	}
	for (int y = low.y; y <= high.y; y++) {
		for (int x = low.x; x <= high.x; x++) {
			if (cells.data[base + uint(y * RECEIVER_GRID_SIZE + x)] >= needed) {
				return true;
			}
		}
	}
	return false;
}
#endif
