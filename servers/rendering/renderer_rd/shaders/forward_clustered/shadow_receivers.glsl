#[compute]

#version 450

#VERSION_DEFINES

// Receiver masks of directional shadow cascades (kb shadow-receiver-culling):
// for each cell of a grid over a cascade's shadow map, the farthest
// distance from the light of any receiver the camera can see there. The
// GPU instance shadow cull then skips casters whose footprint holds no
// receiver behind them.
//
// Default mode: one thread per block of 4 x 4 pixels. The block's nearest
// and farthest depth (the camera's final depth pre-pass) bound its opaque
// surfaces; their part within the cascade's view depths is a frustum
// piece, projected into light space: its bounding rectangle (dilated for
// shadow filtering) takes the largest light distance of its corners. Small
// blocks keep depth discontinuities from smearing receivers over the space
// between a near and a far surface. With volumetric fog (which samples the
// shadow in front of surfaces too) the piece starts at the cascade's
// nearest view depth instead, and empty blocks reach its farthest.
// MODE_BOXES: one thread per visible transparent instance (not in the
// depth pre-pass), which marks its bounds the same way.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1, std430) restrict readonly buffer Grids {
	mat4 clip_to_world; // The camera's, as the depth was rendered.
	vec4 data[]; // RECEIVER_GRID_VEC4S per grid (shadow_receivers_inc.glsl).
}
grids;

layout(set = 0, binding = 2, std430) restrict coherent buffer Cells {
	uint data[];
}
cells;

#ifdef MODE_BOXES
layout(set = 0, binding = 3, std430) restrict readonly buffer Boxes {
	vec4 data[]; // World AABB minimum and maximum (xyz) per box.
}
boxes;
#else
layout(set = 0, binding = 0) uniform sampler2D depth_buffer; // Reverse Z, 0 where nothing was drawn.
#endif

#define RECEIVER_BUILD
#include "shadow_receivers_inc.glsl"

layout(push_constant, std430) uniform Params {
	ivec2 tiles; // Blocks covering the screen.
	ivec2 size; // Screen (internal) size in pixels.
	int level; // A block is 2^level pixels square.
	uint grid_count;
	uint full_rays; // Volumetric fog: receivers from the cascade's nearest view depth.
	uint box_count; // MODE_BOXES.
}
params;

// Most cells are already marked farther by a neighbour: a plain load first
// skips most atomics (the values only grow, so a stale load is harmless).
void raise(uint p_index, uint p_value) {
	if (cells.data[p_index] < p_value) {
		atomicMax(cells.data[p_index], p_value);
	}
}

void mark(uint p_base, ivec2 p_low, ivec2 p_high, uint p_value) {
	ivec2 coarse_low = p_low / RECEIVER_COARSE;
	ivec2 coarse_high = p_high / RECEIVER_COARSE;
	for (int y = coarse_low.y; y <= coarse_high.y; y++) {
		for (int x = coarse_low.x; x <= coarse_high.x; x++) {
			raise(p_base + RECEIVER_COARSE_ALL + uint(y * RECEIVER_COARSE_SIZE + x), p_value);
		}
	}
	ivec2 extent = p_high - p_low + 1;
	if (extent.x * extent.y <= RECEIVER_FINE_LIMIT) {
		for (int y = p_low.y; y <= p_high.y; y++) {
			for (int x = p_low.x; x <= p_high.x; x++) {
				raise(p_base + uint(y * RECEIVER_GRID_SIZE + x), p_value);
			}
		}
	} else {
		// Large footprints mark only coarse cells (bounded work per thread).
		for (int y = coarse_low.y; y <= coarse_high.y; y++) {
			for (int x = coarse_low.x; x <= coarse_high.x; x++) {
				raise(p_base + RECEIVER_COARSE_ONLY + uint(y * RECEIVER_COARSE_SIZE + x), p_value);
			}
		}
	}
}

// Marks the light-space bounds of eight world points as receivers.
void mark_points(uint p_grid, vec3 p_points[8]) {
	uint first = p_grid * RECEIVER_GRID_VEC4S;
	vec4 cell_transform = grids.data[first];
	vec4 light_rows[3] = vec4[](grids.data[first + 1u], grids.data[first + 2u], grids.data[first + 3u]);
	float margin = grids.data[first + 4u].z;
	uint base = p_grid * RECEIVER_GRID_WORDS;
	vec2 cell_low = vec2(1e30);
	vec2 cell_high = vec2(-1e30);
	float farthest = 0.0;
	bool finite = true;
	for (uint i = 0u; i < 8u; i++) {
		vec4 point = vec4(p_points[i], 1.0);
		vec3 light = vec3(dot(light_rows[0], point), dot(light_rows[1], point), dot(light_rows[2], point));
		vec2 cell = light.xy * cell_transform.xy + cell_transform.zw;
		if (any(isnan(point)) || any(isinf(point)) || any(isnan(cell)) || any(isinf(cell)) || isnan(light.z) || isinf(light.z)) {
			finite = false;
		}
		cell_low = min(cell_low, cell);
		cell_high = max(cell_high, cell);
		farthest = max(farthest, -light.z); // The light camera looks along -Z.
	}
	if (!finite) {
		// Degenerate projection: every cell may receive, at any distance.
		mark(base, ivec2(0), ivec2(RECEIVER_GRID_SIZE - 1), floatBitsToUint(1e30));
		return;
	}
	ivec2 low = ivec2(floor(cell_low - margin));
	ivec2 high = ivec2(floor(cell_high + margin));
	if (any(greaterThan(low, ivec2(RECEIVER_GRID_SIZE - 1))) || any(lessThan(high, ivec2(0)))) {
		return; // Outside the cascade's shadow map.
	}
	mark(base, max(low, ivec2(0)), min(high, ivec2(RECEIVER_GRID_SIZE - 1)), receiver_value(farthest));
}

#ifdef MODE_BOXES

void main() {
	uint box = gl_WorkGroupID.x * 64u + gl_LocalInvocationIndex;
	if (box >= params.box_count) {
		return;
	}
	vec3 low = boxes.data[box * 2u].xyz;
	vec3 high = boxes.data[box * 2u + 1u].xyz;
	vec3 points[8];
	for (uint i = 0u; i < 8u; i++) {
		points[i] = vec3((i & 1u) != 0u ? high.x : low.x, (i & 2u) != 0u ? high.y : low.y, (i & 4u) != 0u ? high.z : low.z);
	}
	mark_points(gl_WorkGroupID.y, points);
}

#else

void main() {
	// One thread per block of pixels.
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(block, params.tiles))) {
		return;
	}
	int block_pixels = 1 << params.level;
	ivec2 origin = block * block_pixels;
	float block_near = -1.0; // Reverse Z: larger is nearer. -1 and 2: no surface.
	float block_far = 2.0;
	for (int y = 0; y < block_pixels; y++) {
		for (int x = 0; x < block_pixels; x++) {
			ivec2 pixel = min(origin + ivec2(x, y), params.size - 1);
			float depth = texelFetch(depth_buffer, pixel, 0).r;
			if (isnan(depth) || depth <= 0.0) {
				continue; // Nothing opaque drawn: sky, or what MODE_BOXES and full_rays cover.
			}
			block_near = max(block_near, min(depth, 1.0));
			block_far = min(block_far, depth);
		}
	}
	if (params.full_rays != 0u) {
		block_far = block_near < 0.0 ? 0.0 : block_far; // Empty blocks: to the cascade's farthest depth.
		block_near = 1.0;
	} else if (block_near < 0.0) {
		return; // No opaque surface in the block.
	}
	vec2 uv_low = vec2(origin) / vec2(params.size);
	vec2 uv_high = min(vec2(origin + block_pixels) / vec2(params.size), vec2(1.0));
	for (uint grid = 0u; grid < params.grid_count; grid++) {
		// The block's receivers within this cascade's view depths.
		vec4 ranges = grids.data[grid * RECEIVER_GRID_VEC4S + 4u]; // Near and far clip depth (reverse Z).
		float near_depth = min(block_near, ranges.x);
		float far_depth = max(block_far, ranges.y);
		if (far_depth > near_depth) {
			continue; // None of them lies in this cascade's range.
		}
		vec3 points[8];
		for (uint i = 0u; i < 8u; i++) {
			vec2 uv = vec2((i & 1u) != 0u ? uv_high.x : uv_low.x, (i & 2u) != 0u ? uv_high.y : uv_low.y);
			vec4 world = grids.clip_to_world * vec4(uv * 2.0 - 1.0, (i & 4u) != 0u ? far_depth : near_depth, 1.0);
			points[i] = world.w != 0.0 ? world.xyz / world.w : vec3(uintBitsToFloat(0x7fc00000u)); // NaN marks everything.
		}
		mark_points(grid, points);
	}
}

#endif
