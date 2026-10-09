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

layout(push_constant, std430) uniform Params {
	mat4 inv_projection; // Includes the depth correction used by the depth buffer.
	vec4 cam_rotation[3]; // Rows of the camera-to-world 3x4 transform.
	ivec2 screen_size; // Internal (full) resolution.
	uint flags; // FLAG_HALF_RES: the GI buffer has half the resolution.
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
	imageStore(reflection_buffer, out_pixel, vec4(0.0));
}

#endif // MODE_APPLY

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
