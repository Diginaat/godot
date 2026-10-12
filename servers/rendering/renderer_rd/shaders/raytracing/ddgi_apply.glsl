#[compute]

#version 450

#VERSION_DEFINES

// DDGI screen passes.
//   MODE_APPLY: samples the probes for every opaque pixel (from the depth and
//     normal-roughness buffers of the depth prepass) and writes the diffuse
//     indirect light into the GI ambient buffer, which the Forward+ scene
//     shader reads (INSTANCE_FLAGS_USE_GI_BUFFERS). The reflection buffer
//     gets alpha 0, so specular reflections keep their usual sources.
//   MODE_DEBUG_PROBES: draws the probes as small spheres over the image.
//   MODE_APPLY_LOW, MODE_UPSAMPLE: the low resolution apply
//     (rendering/global_illumination/ddgi/apply_resolution). MODE_APPLY_LOW
//     takes one block of BxB pixels per thread: the depth and normal of the
//     block's main surface (pixels near the depth of its center, averaged),
//     and samples the probes there for the normal and four directions tilted
//     around it. It stores the irradiance and its gradient over the normal.
//     MODE_UPSAMPLE then writes the GI buffer: each pixel takes the nearby
//     blocks whose depth and normal match its own, and evaluates their
//     irradiance at its own normal (so normal maps keep their indirect
//     shading).
//   MODE_MARK: one thread per 8x8 pixel tile marks the probes that light the
//     surface at one pixel of the tile (rotating every frame), so the
//     scheduler can favor them. A separate pass: in the apply pass the
//     marking code alone cost about 0.05 ms at 1080p.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "../oct_inc.glsl"
#include "ddgi_inc.glsl"

layout(set = 0, binding = 0, std140) uniform DDGIUniforms {
	DDGIDataBlock ddgi;
};

layout(set = 0, binding = 1, std430) restrict readonly buffer DDGIProbes {
	DDGIProbe ddgi_probes[];
};

layout(set = 0, binding = 2) uniform texture2D ddgi_irradiance_atlas;
layout(set = 0, binding = 3) uniform texture2D ddgi_distance_atlas;
layout(set = 0, binding = 4) uniform sampler linear_sampler;

layout(set = 0, binding = 5) uniform texture2D depth_buffer;

#ifdef MODE_APPLY
layout(set = 0, binding = 6) uniform texture2D normal_roughness_buffer;
layout(rgba16f, set = 0, binding = 7) uniform restrict writeonly image2D ambient_buffer;
layout(rgba16f, set = 0, binding = 8) uniform restrict writeonly image2D reflection_buffer;
#endif

#ifdef MODE_DEBUG_PROBES
layout(rgba16f, set = 0, binding = 6) uniform restrict image2D color_buffer;
#endif

// Low resolution apply data, one texel per block:
//   0: irradiance at the block normal (rgb), coverage (a)
//   1: gradient of red over the normal (xyz), linear depth (w)
//   2: gradient of green (xyz), octahedral block normal x (w)
//   3: gradient of blue (xyz), octahedral block normal y (w)
#ifdef MODE_APPLY_LOW
layout(set = 0, binding = 6) uniform texture2D normal_roughness_buffer;
layout(rgba16f, set = 0, binding = 10) uniform restrict writeonly image2D low_data0;
layout(rgba16f, set = 0, binding = 11) uniform restrict writeonly image2D low_data1;
layout(rgba16f, set = 0, binding = 12) uniform restrict writeonly image2D low_data2;
layout(rgba16f, set = 0, binding = 13) uniform restrict writeonly image2D low_data3;
#endif

#ifdef MODE_UPSAMPLE
layout(set = 0, binding = 6) uniform texture2D normal_roughness_buffer;
layout(rgba16f, set = 0, binding = 7) uniform restrict writeonly image2D ambient_buffer;
layout(rgba16f, set = 0, binding = 8) uniform restrict writeonly image2D reflection_buffer;
layout(set = 0, binding = 10) uniform texture2D low_data0;
layout(set = 0, binding = 11) uniform texture2D low_data1;
layout(set = 0, binding = 12) uniform texture2D low_data2;
layout(set = 0, binding = 13) uniform texture2D low_data3;
#endif

#ifdef MODE_MARK
layout(set = 0, binding = 6) uniform texture2D normal_roughness_buffer;
// The frame each probe was last marked; the scheduler favors them.
layout(set = 0, binding = 9, std430) restrict buffer DDGISampledFrame {
	uint ddgi_sampled_frame[];
};
#define DDGI_MARK_SAMPLED
#endif

layout(push_constant, std430) uniform Params {
	mat4 inv_projection; // Includes the depth correction used by the depth buffer.
	vec4 cam_rotation[3]; // Rows of the camera-to-world 3x4 transform.
	ivec2 screen_size; // Internal (full) resolution.
	uint flags; // FLAG_HALF_RES: the GI buffer has half the resolution. Bits 8-15: low resolution block size.
	uint debug_mode; // Environment.DDGIDebugMode.
}
params;

#define DDGI_SAMPLER linear_sampler
#define DDGI_SAMPLING
#include "ddgi_sample_inc.glsl"

#define DEBUG_INDIRECT_LIGHT 1u
#define DEBUG_PROBE_IRRADIANCE 2u
#define DEBUG_PROBE_DISTANCE 3u
#define DEBUG_PROBE_STATES 4u
#define DEBUG_PROBE_PRIORITY 5u
#define DEBUG_CASCADES 6u

#define FLAG_HALF_RES 1u

uint low_block_size() {
	return max((params.flags >> 8u) & 0xFFu, 1u);
}

vec3 view_position(ivec2 p_pixel, float p_depth) {
	vec4 pos = vec4((vec2(p_pixel) + 0.5) / vec2(params.screen_size) * 2.0 - 1.0, p_depth, 1.0);
	pos = params.inv_projection * pos;
	return pos.xyz / pos.w;
}

vec3 cascade_color(int p_volume) {
	const vec3 colors[DDGI_MAX_VOLUMES] = vec3[](
			vec3(1.0, 0.25, 0.25), vec3(0.25, 1.0, 0.25), vec3(0.25, 0.5, 1.0), vec3(1.0, 1.0, 0.25),
			vec3(1.0, 0.25, 1.0), vec3(0.25, 1.0, 1.0), vec3(1.0, 0.6, 0.2), vec3(0.7, 0.7, 0.7));
	return p_volume < 0 ? vec3(0.0) : colors[p_volume];
}

#ifdef MODE_APPLY

void main() {
	// Half resolution: each GI buffer texel takes the top-left pixel of its
	// 2x2 block.
	ivec2 out_pixel = ivec2(gl_GlobalInvocationID.xy);
	bool half_res = (params.flags & FLAG_HALF_RES) != 0u;
	ivec2 pixel = half_res ? out_pixel * 2 : out_pixel;
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	float depth = texelFetch(sampler2D(depth_buffer, linear_sampler), pixel, 0).r;
	vec4 normal_roughness = texelFetch(sampler2D(normal_roughness_buffer, linear_sampler), pixel, 0);
	vec3 view_normal = normal_roughness.xyz * 2.0 - 1.0;

	vec4 ambient = vec4(0.0);
	// Reverse Z: the far plane (sky) is at depth 0.
	// The normal is stored "best fit" scaled (not unit length); only zero is invalid.
	if (depth > 0.0 && dot(view_normal, view_normal) > 1e-6) {
		vec3 view_pos = view_position(pixel, depth);
		vec3 world_pos = ddgi_xform(params.cam_rotation, view_pos);
		vec3 normal = normalize(ddgi_xform_dir(params.cam_rotation, normalize(view_normal)));
		vec3 to_camera = normalize(vec3(params.cam_rotation[0].w, params.cam_rotation[1].w, params.cam_rotation[2].w) - world_pos);

		if (params.debug_mode == DEBUG_CASCADES) {
			ambient = vec4(cascade_color(ddgi_volume_at(world_pos)), 1.0);
		} else {
			ambient = ddgi_sample_irradiance(world_pos, normal, to_camera);
			ambient.rgb *= ddgi.volumes[0].params.x; // Energy.
			if (any(isnan(ambient)) || any(isinf(ambient))) {
				ambient = vec4(0.0);
			}
		}
	}

	imageStore(ambient_buffer, out_pixel, ambient);
	// Alpha -1 marks DDGI for the scene shader: it keeps the usual specular
	// sources (alpha 0) but occludes them by the ratio of the DDGI
	// irradiance to the environment's (unoccluded sky) ambient light, so
	// caves and tunnels don't reflect the open sky.
	imageStore(reflection_buffer, out_pixel, vec4(0.0, 0.0, 0.0, -1.0));
}

#endif // MODE_APPLY

#if defined(MODE_APPLY_LOW) || defined(MODE_UPSAMPLE)

// Depth and world normal of a pixel; false for sky and empty pixels.
bool pixel_surface(ivec2 p_pixel, out vec3 r_view_pos, out vec3 r_normal) {
	r_view_pos = vec3(0.0);
	r_normal = vec3(0.0, 1.0, 0.0);
	float depth = texelFetch(sampler2D(depth_buffer, linear_sampler), p_pixel, 0).r;
	vec3 view_normal = texelFetch(sampler2D(normal_roughness_buffer, linear_sampler), p_pixel, 0).xyz * 2.0 - 1.0;
	// Reverse Z: the far plane (sky) is at depth 0.
	if (depth <= 0.0 || dot(view_normal, view_normal) <= 1e-6) {
		return false;
	}
	r_view_pos = view_position(p_pixel, depth);
	r_normal = normalize(ddgi_xform_dir(params.cam_rotation, normalize(view_normal)));
	return true;
}

#endif

#ifdef MODE_APPLY_LOW

void main() {
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	int size = int(low_block_size());
	ivec2 origin = block * size;
	if (any(greaterThanEqual(origin, params.screen_size))) {
		return;
	}
	ivec2 last = min(origin + size, params.screen_size) - 1;

	// The block's surface: 2x2 pixels of the block are read (at a quarter
	// and three quarters of its width; reading all of them cost as much as
	// a full resolution pass of reads, 0.15 ms at 1080p). As in SDFDDGI's
	// min/max checkerboard, alternate blocks take the nearest or the
	// farthest of them as reference, so at an edge both the foreground and
	// the background have blocks nearby for the upsample to match. The
	// pixels within 5% of the reference depth are averaged: stable as the
	// camera moves, and on a surface instead of in the air between an edge
	// and the background.
	int stride = max(size / 2, 1);
	vec3 sample_pos[4];
	vec3 sample_normal[4];
	bool sample_valid[4];
	bool nearest = ((block.x + block.y) & 1) == 0;
	int ref = -1;
	for (int k = 0; k < 4; k++) {
		ivec2 p = min(origin + ivec2(k & 1, k >> 1) * stride + stride / 2, last);
		sample_valid[k] = pixel_surface(p, sample_pos[k], sample_normal[k]);
		if (sample_valid[k] && (ref < 0 || (nearest ? sample_pos[k].z > sample_pos[ref].z : sample_pos[k].z < sample_pos[ref].z))) {
			ref = k; // View space z is negative: larger is nearer.
		}
	}
	bool found = ref >= 0;
	vec4 out0 = vec4(0.0);
	vec4 out1 = vec4(0.0);
	vec4 out2 = vec4(0.0);
	vec4 out3 = vec4(0.0);
	if (found) {
		vec3 ref_pos = sample_pos[ref];
		vec3 ref_normal = sample_normal[ref];
		float ref_depth = -ref_pos.z;
		vec3 pos_sum = vec3(0.0);
		vec3 normal_sum = vec3(0.0);
		float count = 0.0;
		for (int k = 0; k < 4; k++) {
			if (sample_valid[k] && abs(-sample_pos[k].z - ref_depth) <= 0.05 * ref_depth) {
				pos_sum += sample_pos[k];
				normal_sum += sample_normal[k];
				count += 1.0;
			}
		}
		vec3 view_pos = count > 0.0 ? pos_sum / count : ref_pos;
		vec3 normal = dot(normal_sum, normal_sum) > 1e-8 ? normalize(normal_sum) : ref_normal;
		vec3 world_pos = ddgi_xform(params.cam_rotation, view_pos);
		vec3 to_camera = normalize(vec3(params.cam_rotation[0].w, params.cam_rotation[1].w, params.cam_rotation[2].w) - world_pos);

		// Directions tilted about 27 degrees around the normal; central
		// differences give the gradient in the tangent plane.
		float sign_z = normal.z >= 0.0 ? 1.0 : -1.0;
		float a = -1.0 / (sign_z + normal.z);
		float b = normal.x * normal.y * a;
		vec3 t1 = vec3(1.0 + sign_z * normal.x * normal.x * a, sign_z * b, -sign_z * normal.x);
		vec3 t2 = vec3(b, sign_z + normal.y * normal.y * a, -normal.y);
		const float tilt = 0.5;
		vec3 dirs[DDGI_DIRS] = vec3[](normal, normalize(normal + t1 * tilt), normalize(normal - t1 * tilt), normalize(normal + t2 * tilt), normalize(normal - t2 * tilt));
		vec3 e[DDGI_DIRS];
		float coverage = ddgi_sample_irradiance_dirs(world_pos, normal, to_camera, dirs, e);
		float energy = ddgi.volumes[0].params.x;
		float inv_step = 1.0 / (2.0 * tilt / sqrt(1.0 + tilt * tilt));
		vec3 gx = (e[1] - e[2]) * inv_step * energy;
		vec3 gy = (e[3] - e[4]) * inv_step * energy;
		vec2 oct = vec3_to_oct(normal);
		out0 = vec4(e[0] * energy, coverage);
		out1 = vec4(t1 * gx.r + t2 * gy.r, ref_depth);
		out2 = vec4(t1 * gx.g + t2 * gy.g, oct.x);
		out3 = vec4(t1 * gx.b + t2 * gy.b, oct.y);
		if (any(isnan(out0)) || any(isnan(out1)) || any(isnan(out2)) || any(isnan(out3))) {
			out0 = vec4(0.0);
			out1 = vec4(0.0, 0.0, 0.0, ref_depth);
		}
	}
	imageStore(low_data0, block, out0);
	imageStore(low_data1, block, out1);
	imageStore(low_data2, block, out2);
	imageStore(low_data3, block, out3);
}

#endif // MODE_APPLY_LOW

#ifdef MODE_UPSAMPLE

// The blocks a workgroup's 8x8 pixels use, read and decoded once into
// shared memory (10x10 covers 2x2 blocks at half resolution, the worst
// case). Reading 16 texels per pixel instead made the upsample cost about
// as much as the low resolution sampling saved.
#define TILE 10
shared vec4 tile_value[TILE * TILE]; // Irradiance (rgb), coverage (a).
shared vec4 tile_grad_r[TILE * TILE]; // Gradient of red (xyz), linear depth (w).
shared vec3 tile_grad_g[TILE * TILE];
shared vec3 tile_grad_b[TILE * TILE];
shared vec3 tile_normal[TILE * TILE];

void main() {
	ivec2 out_pixel = ivec2(gl_GlobalInvocationID.xy);
	bool half_res = (params.flags & FLAG_HALF_RES) != 0u;
	int step = half_res ? 2 : 1;
	ivec2 pixel = out_pixel * step;
	float size = float(low_block_size());
	ivec2 low_size = textureSize(sampler2D(low_data0, linear_sampler), 0);

	ivec2 first_pixel = ivec2(gl_WorkGroupID.xy) * 8 * step;
	ivec2 tile_base = ivec2(floor((vec2(first_pixel) + 0.5) / size - 0.5));
	for (uint i = gl_LocalInvocationIndex; i < uint(TILE * TILE); i += 64u) {
		ivec2 c = clamp(tile_base + ivec2(int(i) % TILE, int(i) / TILE), ivec2(0), low_size - 1);
		vec4 d0 = texelFetch(sampler2D(low_data0, linear_sampler), c, 0);
		vec4 d1 = texelFetch(sampler2D(low_data1, linear_sampler), c, 0);
		vec4 d2 = texelFetch(sampler2D(low_data2, linear_sampler), c, 0);
		vec4 d3 = texelFetch(sampler2D(low_data3, linear_sampler), c, 0);
		tile_value[i] = d0;
		tile_grad_r[i] = d1;
		tile_grad_g[i] = d2.xyz;
		tile_grad_b[i] = d3.xyz;
		// vec3_to_oct() encodes to 0..1, oct_to_vec3() decodes -1..1.
		tile_normal[i] = oct_to_vec3(vec2(d2.w, d3.w) * 2.0 - 1.0);
	}
	barrier();

	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}
	vec4 ambient = vec4(0.0);
	vec3 view_pos;
	vec3 normal;
	if (pixel_surface(pixel, view_pos, normal)) {
		float depth = -view_pos.z;
		// The 2x2 blocks around the pixel, bilinear, times how well each
		// block's surface matches the pixel's (depth, normal). If none
		// matches (a thin object smaller than a block), the closest match.
		vec2 low_pos = (vec2(pixel) + 0.5) / size - 0.5;
		ivec2 base = ivec2(floor(low_pos));
		vec2 f = low_pos - vec2(base);
		vec4 sum = vec4(0.0);
		float weight_sum = 0.0;
		vec4 best = vec4(0.0);
		float best_match = -1.0;
		for (int y = 0; y <= 1; y++) {
			for (int x = 0; x <= 1; x++) {
				ivec2 t = clamp(base + ivec2(x, y) - tile_base, ivec2(0), ivec2(TILE - 1));
				int i = t.y * TILE + t.x;
				float block_depth = tile_grad_r[i].w;
				if (block_depth <= 0.0) {
					continue; // Sky block.
				}
				float depth_match = max(0.0, 1.0 - abs(block_depth - depth) / (0.05 * depth));
				float normal_match = pow(max(dot(normal, tile_normal[i]), 0.0), 8.0);
				float match = depth_match * depth_match * normal_match;
				// Closeness in depth breaks ties when nothing matches.
				float score = match + 0.001 / (1.0 + abs(block_depth - depth));
				vec3 dn = normal - tile_normal[i];
				vec4 value = vec4(max(tile_value[i].rgb + vec3(dot(tile_grad_r[i].xyz, dn), dot(tile_grad_g[i], dn), dot(tile_grad_b[i], dn)), vec3(0.0)), tile_value[i].a);
				if (score > best_match) {
					best_match = score;
					best = value;
				}
				vec2 bl = mix(1.0 - f, f, vec2(x, y));
				float w = bl.x * bl.y * match;
				sum += value * w;
				weight_sum += w;
			}
		}
		ambient = weight_sum > 1e-4 ? sum / weight_sum : best;
	}
	imageStore(ambient_buffer, out_pixel, ambient);
	// As in MODE_APPLY.
	imageStore(reflection_buffer, out_pixel, vec4(0.0, 0.0, 0.0, -1.0));
}

#endif // MODE_UPSAMPLE

#ifdef MODE_MARK

void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	// A different pixel of the tile every frame (8 x 8 positions over 64
	// frames, well spread), so surfaces smaller than a tile get marked too.
	uint f = ddgi.atlas.w;
	ivec2 pixel = tile * 8 + ivec2((f * 3u) & 7u, (f * 5u + (f >> 3u)) & 7u);
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}
	float depth = texelFetch(sampler2D(depth_buffer, linear_sampler), pixel, 0).r;
	vec3 view_normal = texelFetch(sampler2D(normal_roughness_buffer, linear_sampler), pixel, 0).xyz * 2.0 - 1.0;
	if (depth <= 0.0 || dot(view_normal, view_normal) <= 1e-6) {
		return;
	}
	vec3 world_pos = ddgi_xform(params.cam_rotation, view_position(pixel, depth));
	vec3 normal = normalize(ddgi_xform_dir(params.cam_rotation, normalize(view_normal)));
	vec3 to_camera = normalize(vec3(params.cam_rotation[0].w, params.cam_rotation[1].w, params.cam_rotation[2].w) - world_pos);
	ddgi_mark_sampled_probes(world_pos, normal, to_camera, f);
}

#endif // MODE_MARK

#ifdef MODE_DEBUG_PROBES

// Probe spheres, found by walking the cells around each probe (one probe
// per cell) along the view ray with a 3D DDA.
void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	float depth = texelFetch(sampler2D(depth_buffer, linear_sampler), pixel, 0).r;
	vec3 view_far = view_position(pixel, max(depth, 0.000001));
	vec3 cam_pos = vec3(params.cam_rotation[0].w, params.cam_rotation[1].w, params.cam_rotation[2].w);
	vec3 world_end = ddgi_xform(params.cam_rotation, view_far);
	vec3 ray_dir = normalize(world_end - cam_pos);
	float max_t = length(world_end - cam_pos);

	float best_t = max_t;
	vec3 best_color = vec3(0.0);
	bool found = false;

	// The finest volume only: coarser cascades would cover the view with large spheres.
	for (uint v = 0u; v < min(ddgi.counts.x, 1u); v++) {
		DDGIVolume vol = ddgi.volumes[v];
		float spacing = vol.spacing.x;
		float radius = spacing * 0.1;
		vec3 grid_half = (vec3(vol.grid.xyz) - 1.0) * 0.5;

		// Ray in grid units, where probe i sits at integer coordinate i.
		vec3 o = ddgi_xform(vol.world_to_local, cam_pos) / spacing + grid_half;
		vec3 d = ddgi_xform_dir(vol.world_to_local, ray_dir) / spacing;

		// Clip the ray to the grid bounds (plus half a cell).
		vec3 box_min = vec3(-0.5), box_max = vec3(vol.grid.xyz) - 0.5;
		vec3 inv_d = 1.0 / max(abs(d), vec3(1e-8)) * sign(d + vec3(1e-12));
		vec3 t0 = (box_min - o) * inv_d, t1 = (box_max - o) * inv_d;
		float t_enter = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
		float t_exit = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
		t_exit = min(t_exit, best_t);
		if (t_enter >= t_exit) {
			continue;
		}

		vec3 p = o + d * (t_enter + 0.0001);
		ivec3 cell = ivec3(floor(p + 0.5));
		ivec3 step_dir = ivec3(sign(d + vec3(1e-12)));
		vec3 next_boundary = vec3(cell) + 0.5 * vec3(step_dir);
		vec3 t_max = (next_boundary - o) * inv_d;
		vec3 t_delta = abs(inv_d);

		for (int i = 0; i < 256; i++) {
			if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, vol.grid.xyz))) {
				break;
			}
			uint probe = ddgi_probe_index(vol, cell);
			DDGIProbe pd = ddgi_probes[probe];
			vec3 center = ddgi_probe_world_position(vol, cell, pd.offset);
			vec3 oc = cam_pos - center;
			float b = dot(oc, ray_dir);
			float c = dot(oc, oc) - radius * radius;
			float h = b * b - c;
			if (h >= 0.0) {
				float t = -b - sqrt(h);
				if (t > 0.0 && t < best_t) {
					best_t = t;
					found = true;
					vec3 n = normalize(cam_pos + ray_dir * t - center);
					if (params.debug_mode == DEBUG_PROBE_IRRADIANCE) {
						best_color = textureLod(sampler2D(ddgi_irradiance_atlas, linear_sampler), ddgi_atlas_uv(probe, n, ddgi.atlas.x, ddgi.atlas.z, ddgi.atlas_inv_size.xy), 0.0).rgb;
					} else if (params.debug_mode == DEBUG_PROBE_DISTANCE) {
						float mean = textureLod(sampler2D(ddgi_distance_atlas, linear_sampler), ddgi_atlas_uv(probe, n, ddgi.atlas.y, ddgi.atlas.z, ddgi.atlas_inv_size.zw), 0.0).r;
						best_color = vec3(clamp(mean / max(vol.spacing.w, 0.001), 0.0, 1.0));
					} else if (params.debug_mode == DEBUG_PROBE_STATES) {
						const vec3 state_colors[5] = vec3[](vec3(0.2, 0.4, 1.0), vec3(0.2, 1.0, 0.2), vec3(0.4, 0.4, 0.4), vec3(1.0, 0.15, 0.15), vec3(1.0, 0.6, 0.1));
						best_color = state_colors[min(pd.state, 4u)];
					} else {
						// Update priority: green = stable, red = changing; white = traced this frame.
						best_color = mix(vec3(0.1, 0.8, 0.1), vec3(1.0, 0.1, 0.1), clamp(pd.variability * 4.0, 0.0, 1.0));
						if (pd.last_update_frame == ddgi.atlas.w) {
							best_color = vec3(1.0);
						}
					}
					// Simple shading so the spheres read as spheres.
					best_color *= 0.6 + 0.4 * max(dot(n, -ray_dir), 0.0);
				}
			}
			// Step to the next cell.
			if (t_max.x < t_max.y && t_max.x < t_max.z) {
				if (t_max.x > t_exit) {
					break;
				}
				cell.x += step_dir.x;
				t_max.x += t_delta.x;
			} else if (t_max.y < t_max.z) {
				if (t_max.y > t_exit) {
					break;
				}
				cell.y += step_dir.y;
				t_max.y += t_delta.y;
			} else {
				if (t_max.z > t_exit) {
					break;
				}
				cell.z += step_dir.z;
				t_max.z += t_delta.z;
			}
		}
	}

	if (found) {
		imageStore(color_buffer, pixel, vec4(best_color, 1.0));
	}
}

#endif // MODE_DEBUG_PROBES
