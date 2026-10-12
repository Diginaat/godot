#[compute]

#version 460

#VERSION_DEFINES

#ifdef DDGI_EMITTERS
#extension GL_EXT_ray_query : enable
#endif

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
//   DDGI_EMITTERS (with MODE_APPLY or MODE_APPLY_LOW): emissive surfaces
//     light the surface directly, as sphere lights with a ray traced
//     shadow (rendering/global_illumination/ddgi/emissive_lights). The
//     probe rays then leave the emitters' own light out (it would count
//     twice) and only carry what they light, as bounce light.
//   Near-field rays (rendering/global_illumination/ddgi/near_field_rays,
//     with the low resolution apply): probes hold the light at points about
//     a probe spacing apart, smoothed over many frames, so light from a
//     nearby object (a red ball next to a white wall) shows as a soft blob
//     at the probes around it that trails behind it when it moves.
//     MODE_APPLY_LOW then also writes one cosine-distributed ray per block
//     (FLAG_NEAR_FIELD), traced up to a few probe spacings by the ray
//     tracing pipeline as probe rays are (scene_raytracing_raygen.glsl).
//     MODE_NF_RESOLVE adds the probes' light at each hit (bounce light); rays
//     that hit nothing within range take the probes' irradiance at the
//     surface (far field). Moving objects are left out of the probes and
//     these rays; each block also traces one ray toward the moving object
//     that covers most of its view and gets its light (bounce and shadow)
//     from the object's shape (a sphere, or its box scaled by how much of it
//     the mesh fills), without noise or lag. MODE_NF_TEMPORAL accumulates the samples
//     over frames (reprojected, history clamped to the neighborhood, so
//     changes show within a few frames), MODE_NF_FILTER smooths them with
//     an edge-aware blur (two passes) and puts them in place of the probes'
//     light in the low resolution data.
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

#ifdef DDGI_EMITTERS
layout(set = 0, binding = 14) uniform accelerationStructureEXT ddgi_tlas;
// Two vec4 per emitter: center (xyz) and radius of the sphere with the
// same surface area (w); radiance (rgb) and bounding radius (w).
layout(set = 0, binding = 15, std430) restrict readonly buffer DDGIEmitters {
	vec4 ddgi_emitters[];
};
#endif

#if defined(MODE_APPLY_LOW) || defined(MODE_NF_RESOLVE) || defined(MODE_NF_TEMPORAL) || defined(MODE_NF_FILTER)
// Near-field rays (MODE_APPLY_LOW writes, the trace and MODE_NF_RESOLVE
// read): [0] header (block count, max distance, width, height), then per
// block the cosine ray, then per block the ray toward the moving object that
// covers most of the view: origin (xyz float bits) and direction
// (octahedral unorm16 x 2; 0: no ray).
layout(set = 0, binding = 16, std430) restrict buffer DDGINearFieldRays {
	uvec4 nf_rays[];
};
layout(set = 0, binding = 18, std140) uniform DDGINearFieldParams {
	mat4 prev_view_projection; // World to the previous frame's clip space.
	vec4 params; // x: max ray distance, y: frame, z: minimum history blend, w: history valid (0 after a reset).
	ivec4 size; // xy: low resolution size, z: block size, w: moving objects.
}
nf;
#endif

#ifdef MODE_APPLY_LOW
// Probe irradiance without emitters (rgb) and the form factor of the
// nearest moving object (a), for the near-field passes.
layout(rgba16f, set = 0, binding = 17) uniform restrict writeonly image2D nf_probe_out;
// The visible faces' form factors (rgb) and object and face code (a).
layout(rgba16f, set = 0, binding = 28) uniform restrict writeonly image2D nf_object_weight_out;
// Shapes of the objects that keep moving, 4 vec4 each: center (xyz) and
// fill (w), half axes x (xyz; w 1 for a sphere), y and z (RenderRaytracing::
// ddgi_moving).
layout(set = 0, binding = 26, std430) restrict readonly buffer DDGINearFieldMoving {
	vec4 nf_moving[];
};

#endif

#ifdef MODE_NF_RESOLVE
layout(set = 0, binding = 17) uniform texture2D nf_probe;
// Radiance (rgb), hit distance (a); the object rays in the lower half.
layout(set = 0, binding = 19) uniform texture2D nf_ray_data;
layout(set = 0, binding = 20, std430) restrict readonly buffer DDGINearFieldBounce {
	uvec4 nf_bounce[]; // As DDGIRayBounce, z 1 for a hit on a moving object, w the hit distance; then the object rays.
};
layout(rgba16f, set = 0, binding = 21) uniform restrict writeonly image2D nf_raw_out;
// The moving objects' light (rgb): bounce light minus the light they hide.
layout(rgba16f, set = 0, binding = 27) uniform restrict writeonly image2D nf_object_out;
layout(set = 0, binding = 28) uniform texture2D nf_object_weight;
layout(set = 0, binding = 26, std430) restrict readonly buffer DDGINearFieldMoving {
	vec4 nf_moving[];
};
// The light leaving each moving object's six sides (box faces, or the
// directions of its axes for a sphere), gathered from the object rays that
// hit it: rgb sums (fixed point, NF_BIN_SCALE) and a count, per object and
// side. Last frame's, to read; this frame's, to add to.
#define NF_BIN_SCALE 64.0
layout(set = 0, binding = 29, std430) restrict readonly buffer DDGINearFieldBinsPrevious {
	uint nf_bins_previous[];
};
layout(set = 0, binding = 30, std430) restrict buffer DDGINearFieldBins {
	uint nf_bins[];
};
#endif

#ifdef MODE_NF_TEMPORAL
layout(rgba16f, set = 0, binding = 10) uniform restrict readonly image2D low_data0;
layout(rgba16f, set = 0, binding = 11) uniform restrict readonly image2D low_data1;
layout(rgba16f, set = 0, binding = 12) uniform restrict readonly image2D low_data2;
layout(rgba16f, set = 0, binding = 13) uniform restrict readonly image2D low_data3;
layout(set = 0, binding = 21) uniform texture2D nf_raw;
layout(set = 0, binding = 22) uniform texture2D nf_history;
layout(rgba16f, set = 0, binding = 23) uniform restrict writeonly image2D nf_history_out;
// Previous frame: linear depth (negative when a moving object was near),
// octahedral normal, a moving average of the probes' luminance at the block.
layout(set = 0, binding = 24) uniform texture2D nf_geometry;
layout(set = 0, binding = 17) uniform texture2D nf_probe; // This frame's probe irradiance at the block.
layout(rgba16f, set = 0, binding = 25) uniform restrict writeonly image2D nf_geometry_out;
// How noisy the accumulated light still is (r: standard error, luminance).
layout(rgba16f, set = 0, binding = 31) uniform restrict writeonly image2D nf_noise_out;
#endif

#ifdef MODE_NF_FILTER
layout(rgba16f, set = 0, binding = 10) uniform restrict image2D low_data0;
layout(rgba16f, set = 0, binding = 11) uniform restrict image2D low_data1;
layout(rgba16f, set = 0, binding = 12) uniform restrict image2D low_data2;
layout(rgba16f, set = 0, binding = 13) uniform restrict image2D low_data3;
layout(set = 0, binding = 17) uniform texture2D nf_probe;
layout(set = 0, binding = 22) uniform texture2D nf_history; // This frame's accumulated light (or the first pass's result).
layout(set = 0, binding = 31) uniform texture2D nf_noise; // MODE_NF_TEMPORAL.
layout(rgba16f, set = 0, binding = 21) uniform restrict writeonly image2D nf_filter_out; // The first pass's result.
layout(set = 0, binding = 27) uniform texture2D nf_object; // The moving objects' light (MODE_NF_RESOLVE).
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
	uint flags; // FLAG_HALF_RES: the GI buffer has half the resolution. Bits 8-15: low resolution block size. Bits 16-24: emitters.
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
#define FLAG_NEAR_FIELD (1u << 25u)
// MODE_NF_FILTER: the second (final) pass, with taps 2 blocks apart.
#define FLAG_NF_FILTER_FINAL (1u << 28u)

uint low_block_size() {
	return max((params.flags >> 8u) & 0xFFu, 1u);
}

// Light from a sphere of radiance 1 that fills sin^2 = p_sin2 of the view
// from a surface whose normal makes cos = p_cos with the direction to it,
// as mean radiance over the cosine lobe (what the probes store): the
// sphere light illuminance of Lagarde and de Rousiers ("Moving Frostbite
// to PBR", 2014) over pi, which handles a sphere partly below the
// horizon.
float sphere_light(float p_cos, float p_sin2) {
	if (p_cos * p_cos > p_sin2) {
		return p_sin2 * max(p_cos, 0.0);
	}
	float sin_t = sqrt(max(1.0 - p_cos * p_cos, 1e-8));
	float x = sqrt(max(1.0 / p_sin2 - 1.0, 1e-8));
	float y = clamp(-x * (p_cos / sin_t), -1.0, 1.0);
	float sin_sqrt_y = sin_t * sqrt(max(1.0 - y * y, 0.0));
	return max(((p_cos * acos(y) - x * sin_sqrt_y) * p_sin2 + atan(sin_sqrt_y / x)) / 3.14159265359, 0.0);
}

#ifdef MODE_APPLY_LOW
// One edge of Lambert's formula for unit vectors p_a, p_b: their cross
// product times angle / (2 pi sin(angle)), from a rational fit instead of
// acos (the fit includes the 1 / 2 pi; acos per edge made this pass 1 ms
// slower).
vec3 polygon_edge(vec3 p_a, vec3 p_b) {
	float x = dot(p_a, p_b);
	float y = abs(x);
	float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
	float b = 3.4175940 + (4.1616724 + y) * y;
	float v = a / b;
	float theta_over_sin = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
	return cross(p_a, p_b) * theta_over_sin;
}

// Form factor (cosine-weighted share of the hemisphere above normal p_n) of
// a quad given by its corners relative to the point, clipped at the
// horizon: Lambert's formula, a sum over the edges.
float polygon_form_factor(vec3 p_c0, vec3 p_c1, vec3 p_c2, vec3 p_c3, vec3 p_n) {
	float d0 = dot(p_c0, p_n);
	float d1 = dot(p_c1, p_n);
	float d2 = dot(p_c2, p_n);
	float d3 = dot(p_c3, p_n);
	if (max(max(d0, d1), max(d2, d3)) <= 0.0) {
		return 0.0; // All below the horizon.
	}
	vec3 sum;
	if (min(min(d0, d1), min(d2, d3)) >= 0.0) {
		// All above the horizon (the usual case): no clipping.
		vec3 a = normalize(p_c0);
		vec3 b = normalize(p_c1);
		vec3 c = normalize(p_c2);
		vec3 d = normalize(p_c3);
		sum = polygon_edge(a, b) + polygon_edge(b, c) + polygon_edge(c, d) + polygon_edge(d, a);
	} else {
		// Clip against the horizon plane: at most 5 corners.
		vec3 corners[4] = vec3[](p_c0, p_c1, p_c2, p_c3);
		float dots[4] = float[](d0, d1, d2, d3);
		vec3 clipped[5];
		int count = 0;
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) & 3;
			if (dots[i] >= 0.0) {
				clipped[count++] = corners[i];
			}
			if ((dots[i] >= 0.0) != (dots[j] >= 0.0)) {
				clipped[count++] = corners[i] + (corners[j] - corners[i]) * (dots[i] / (dots[i] - dots[j]));
			}
		}
		sum = vec3(0.0);
		for (int i = 0; i < count; i++) {
			sum += polygon_edge(normalize(clipped[i]), normalize(clipped[(i + 1) % count]));
		}
	}
	return abs(dot(sum, p_n));
}

// Form factors of a moving object's shape seen from p_pos with normal
// p_normal: a sphere has one (r_faces[0]), a box one per axis whose face
// faces the point (r_sides: bit per axis, set for the + face), already
// scaled by how much of the box the mesh fills. r_targets: a point on each
// face. r_distance: to the closest point.
// Returns the total, or a negative value when p_pos is on or in the shape.
float moving_form_factor(int p_index, vec3 p_pos, vec3 p_normal, out vec3 r_targets[3], out float r_faces[3], out uint r_sides, out float r_distance) {
	vec4 c = nf_moving[p_index * 4];
	vec4 ax = nf_moving[p_index * 4 + 1];
	vec4 ay = nf_moving[p_index * 4 + 2];
	vec4 az = nf_moving[p_index * 4 + 3];
	vec3 axes[3] = vec3[](ax.xyz, ay.xyz, az.xyz);
	r_targets = vec3[](c.xyz, c.xyz, c.xyz);
	r_faces = float[](0.0, 0.0, 0.0);
	r_sides = 0u;
	vec3 rel = p_pos - c.xyz;
	// The closest point on the box, in its axes.
	vec3 local;
	vec3 closest = c.xyz;
	for (int i = 0; i < 3; i++) {
		float len2 = max(dot(axes[i], axes[i]), 1e-12);
		local[i] = dot(rel, axes[i]) / len2; // -1..1 inside.
		closest += axes[i] * clamp(local[i], -1.0, 1.0);
	}
	r_distance = length(p_pos - closest);
	if (ax.w > 0.5) {
		// Sphere: exact.
		float radius = (length(ax.xyz) + length(ay.xyz) + length(az.xyz)) / 3.0;
		vec3 to_center = c.xyz - p_pos;
		float d = length(to_center);
		r_distance = d - radius;
		if (r_distance < 0.02) {
			return -1.0; // On or in the object: it doesn't light itself.
		}
		r_faces[0] = sphere_light(dot(p_normal, to_center / d), radius * radius / (d * d));
		return r_faces[0];
	}
	if (r_distance < 0.02) {
		return -1.0; // On or in the object: it doesn't light itself.
	}
	// The box faces that face the point; a convex box's visible faces don't
	// overlap, so their form factors add up. Each face gets its own ray, so
	// a lit and a shaded face both count (one ray for the whole box jumped
	// between them and left hard edges).
	float total = 0.0;
	for (int i = 0; i < 3; i++) {
		if (abs(local[i]) <= 1.0) {
			continue; // Neither face on this axis faces the point.
		}
		vec3 face_center = c.xyz + axes[i] * sign(local[i]);
		vec3 face = face_center - p_pos;
		vec3 u = axes[(i + 1) % 3];
		vec3 v = axes[(i + 2) % 3];
		// How much of the box the mesh covers on average.
		r_faces[i] = polygon_form_factor(face - u - v, face + u - v, face + u + v, face - u + v, p_normal) * c.w;
		// A bit inside the face: on the mesh even where it doesn't fill the
		// box's corners (a capsule's ends).
		r_targets[i] = mix(face_center, c.xyz, 0.25);
		r_sides |= local[i] > 0.0 ? (1u << uint(i)) : 0u;
		total += r_faces[i];
	}
	return total;
}
#endif

#ifdef DDGI_EMITTERS
// Direct light from the emissive surfaces at p_pos for each of the first
// p_count directions of p_dirs (surface normals), added to r_light. One
// shadow ray per emitter, toward its center, shared by the directions.
void add_emitter_light(vec3 p_pos, vec3 p_normal, vec3 p_dirs[DDGI_DIRS], int p_count, inout vec3 r_light[DDGI_DIRS]) {
	uint count = (params.flags >> 16u) & 0x1FFu;
	vec3 origin = p_pos + p_normal * 0.02;
	for (uint i = 0u; i < count; i++) {
		vec4 sphere = ddgi_emitters[i * 2u];
		vec4 radiance = ddgi_emitters[i * 2u + 1u];
		vec3 to_emitter = sphere.xyz - p_pos;
		float dist2 = dot(to_emitter, to_emitter);
		float r2 = sphere.w * sphere.w;
		if (dist2 <= r2) {
			continue; // Inside the emitter's bounds.
		}
		float sin2 = r2 / dist2;
		if (sin2 * max(radiance.r, max(radiance.g, radiance.b)) < 1e-4) {
			continue; // Too far to matter.
		}
		float dist = sqrt(dist2);
		vec3 l = to_emitter / dist;
		float facing = 0.0;
		for (int k = 0; k < p_count; k++) {
			facing = max(facing, dot(p_dirs[k], l));
		}
		if (facing * facing <= -sin2 && facing < 0.0) {
			continue; // Below every direction's horizon.
		}
		// Stops at the emitter's bounds, so the ray doesn't hit the emitter.
		float t_max = dist - radiance.w;
		if (t_max > 0.03) {
			rayQueryEXT query;
			rayQueryInitializeEXT(query, ddgi_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, l, t_max);
			while (rayQueryProceedEXT(query)) {
			}
			if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
				continue;
			}
		}
		for (int k = 0; k < p_count; k++) {
			r_light[k] += radiance.rgb * sphere_light(dot(p_dirs[k], l), sin2);
		}
	}
}
#endif

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
#ifdef DDGI_EMITTERS
			if (ambient.a > 0.0) {
				vec3 dirs[DDGI_DIRS] = vec3[](normal, normal, normal, normal, normal);
				vec3 light[DDGI_DIRS] = vec3[](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
				add_emitter_light(world_pos, normal, dirs, 1, light);
				// The scene shader blends the GI buffer by its coverage.
				ambient.rgb += light[0] / ambient.a;
			}
#endif
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
#ifdef DDGI_EMITTERS
		if (coverage > 0.0) {
			vec3 light[DDGI_DIRS] = vec3[](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
			add_emitter_light(world_pos, normal, dirs, DDGI_DIRS, light);
			for (int k = 0; k < DDGI_DIRS; k++) {
				e[k] += light[k] / coverage;
			}
		}
#endif
		if ((params.flags & FLAG_NEAR_FIELD) != 0u) {
			// A cosine-distributed direction: 4x4 neighboring blocks take
			// one stratum each of a 4x4 grid, shifted every frame (R2
			// sequence), so a small neighborhood covers the hemisphere.
			uint stratum = uint(block.x & 3) + uint(block.y & 3) * 4u;
			uint h = (uint(block.x) * 73856093u) ^ (uint(block.y) * 19349663u);
			h = (h ^ (h >> 13u)) * 0x5bd1e995u;
			vec2 jitter = vec2(float(h & 0xFFFFu), float(h >> 16u)) / 65536.0;
			float frame = nf.params.y;
			vec2 r2 = fract(vec2(0.7548776662, 0.5698402910) * frame);
			vec2 u = fract((vec2(stratum & 3u, stratum >> 2u) + jitter) * 0.25 + r2);

			float phi = 6.28318530718 * u.x;
			float sin_t = sqrt(u.y);
			vec3 dir = normalize(t1 * (cos(phi) * sin_t) + t2 * (sin(phi) * sin_t) + normal * sqrt(max(1.0 - u.y, 0.0)));

			// Moving objects (a ball rolling by): the probes and the cosine
			// rays leave them out (rt_cull_mask()), so their own light can be
			// averaged over many frames. The nearest moving object (by solid
			// angle) gets a second ray, toward its center: the light leaving
			// it there, spread as from a sphere of that radiance (as the
			// emissive lights are), minus the light it hides, is its bounce
			// light and shadow. The same every frame: no noise, no lag.
			// The moving object with the largest form factor, faded out over
			// the second half of the range so the range doesn't show as an
			// edge.
			// The object that covers most of the view: its faces' form
			// factors (rgb) and which object and faces (a: index * 8 + side
			// bits + 1; 0 for none), and one ray toward it that tells whether
			// it is in view (and gathers its light, MODE_NF_RESOLVE).
			float form_factor = 0.0;
			vec3 object_faces = vec3(0.0);
			float object_code = 0.0;
			vec3 object_target = world_pos;
			// The pick, by a cheap estimate (the solid angle of a sphere of
			// the object's mean size); the exact form factor only for it.
			int pick = -1;
			float pick_estimate = 0.0;
			for (int i = 0; i < nf.size.w; i++) {
				vec4 c = nf_moving[i * 4];
				float size = (length(nf_moving[i * 4 + 1].xyz) + length(nf_moving[i * 4 + 2].xyz) + length(nf_moving[i * 4 + 3].xyz)) / 3.0;
				vec3 to_object = c.xyz - world_pos;
				float d2 = dot(to_object, to_object);
				float reach = nf.params.x + 1.75 * size; // Range plus the box's half diagonal at most.
				if (d2 > reach * reach || dot(to_object, normal) < -1.75 * size) {
					continue; // Out of range, or below the horizon.
				}
				float estimate = size * size / max(d2, 1e-4);
				if (estimate > pick_estimate) {
					pick_estimate = estimate;
					pick = i;
				}
			}
			if (pick >= 0) {
				vec3 targets[3];
				float faces[3];
				uint sides;
				float distance;
				float f = moving_form_factor(pick, world_pos, normal, targets, faces, sides, distance);
				if (f > 0.0 && distance <= nf.params.x) {
					float fade = 1.0 - smoothstep(0.5 * nf.params.x, nf.params.x, distance);
					form_factor = f * fade;
					object_faces = vec3(faces[0], faces[1], faces[2]) * fade;
					object_code = float(uint(pick) * 8u + sides) + 1.0;
					// Toward the visible faces, weighted by their share.
					object_target = (targets[0] * faces[0] + targets[1] * faces[1] + targets[2] * faces[2]) / f;
				}
			}
			imageStore(nf_object_weight_out, block, vec4(object_faces, object_code));

			imageStore(nf_probe_out, block, vec4(e[0], form_factor));
			// Off the surface by a bit more far away (depth precision).
			vec3 ray_origin = world_pos + normal * max(0.01, 0.002 * ref_depth);
			uint idx = uint(block.y) * uint(nf.size.x) + uint(block.x);
			nf_rays[1u + idx] = uvec4(floatBitsToUint(ray_origin), max(packUnorm2x16(vec3_to_oct(dir)), 1u));
			uint blocks = uint(nf.size.x * nf.size.y);
			vec3 to_target = object_target - world_pos;
			bool valid = form_factor > 0.0 && dot(to_target, to_target) > 1e-8;
			nf_rays[1u + blocks + idx] = valid ? uvec4(floatBitsToUint(ray_origin), max(packUnorm2x16(vec3_to_oct(normalize(to_target))), 1u)) : uvec4(0u);
		}
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
	if (!found && (params.flags & FLAG_NEAR_FIELD) != 0u) {
		nf_rays[1u + uint(block.y) * uint(nf.size.x) + uint(block.x)] = uvec4(0u);
		nf_rays[1u + uint(nf.size.x * nf.size.y) + uint(block.y) * uint(nf.size.x) + uint(block.x)] = uvec4(0u);
		imageStore(nf_object_weight_out, block, vec4(0.0));
		imageStore(nf_probe_out, block, vec4(0.0));
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

#if defined(MODE_NF_TEMPORAL) || defined(MODE_NF_FILTER)

// World position of the surface at a block, from its linear depth.
vec3 nf_block_world_position(ivec2 p_block, float p_depth) {
	vec2 pixel = (vec2(p_block) + 0.5) * float(nf.size.z);
	vec4 v = params.inv_projection * vec4(pixel / vec2(params.screen_size) * 2.0 - 1.0, 0.5, 1.0);
	vec3 view = v.xyz / v.w;
	view *= p_depth / max(-view.z, 1e-6);
	return ddgi_xform(params.cam_rotation, view);
}

// How well two blocks' surfaces match (1: same surface).
float nf_geometry_weight(float p_depth, vec3 p_normal, float p_other_depth, vec3 p_other_normal) {
	if (p_other_depth <= 0.0) {
		return 0.0;
	}
	float depth_match = max(0.0, 1.0 - abs(p_other_depth - p_depth) / (0.05 * p_depth));
	float normal_match = max(dot(p_normal, p_other_normal), 0.0);
	normal_match *= normal_match;
	return depth_match * normal_match * normal_match;
}

#endif

#ifdef MODE_NF_RESOLVE

// A moving object's light is its emission and direct light only, not the
// probes' light it passes on: the probes don't see the object, so under a
// ball they count the floor as lit and the ball's shaded underside came out
// bright red. Against the path tracer, direct light alone matched (red
// beside and above the ball -12.7 vs -12.6, was -3).

void main() {
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(block, nf.size.xy))) {
		return;
	}
	uint idx = uint(block.y) * uint(nf.size.x) + uint(block.x);
	uvec4 r = nf_rays[1u + idx];
	if (r.w == 0u) {
		imageStore(nf_raw_out, block, vec4(0.0));
		imageStore(nf_object_out, block, vec4(0.0));
		return;
	}
	vec4 probe = texelFetch(sampler2D(nf_probe, linear_sampler), block, 0);

	// The moving object that covers most of the view: its visible sides'
	// light times their form factors (MODE_APPLY_LOW), minus the light it
	// hides. The side's light is averaged over every object ray that hit
	// that side last frame, so it is smooth even where the object's lighting
	// changes across it (a shadow edge); one ray per block only tells whether
	// the object is in view, and adds to this frame's averages.
	vec3 object_light = vec3(0.0);
	uint blocks = uint(nf.size.x * nf.size.y);
	vec4 weights = texelFetch(sampler2D(nf_object_weight, linear_sampler), block, 0);
	uvec4 object_ray = nf_rays[1u + blocks + idx];
	if (weights.a > 0.5 && object_ray.w != 0u) {
		uint code = uint(weights.a - 0.5);
		int object = int(code / 8u);
		uint sides = code % 8u;
		vec4 hit = texelFetch(sampler2D(nf_ray_data, linear_sampler), block + ivec2(0, nf.size.y), 0);
		uvec4 b = nf_bounce[blocks + idx];
		if (object < nf.size.w && hit.a > 0.0 && hit.a < DDGI_MISS_DISTANCE * 0.5 && b.z != 0u) {
			vec3 object_dir = oct_to_vec3(unpackUnorm2x16(object_ray.w) * 2.0 - 1.0);
			vec3 leaving = max(hit.rgb, vec3(0.0));
			float lum = ddgi_luminance(leaving);
			if (lum > ddgi.schedule.z) {
				leaving *= ddgi.schedule.z / lum;
			}
			vec3 axes[3] = vec3[](normalize(nf_moving[object * 4 + 1].xyz), normalize(nf_moving[object * 4 + 2].xyz), normalize(nf_moving[object * 4 + 3].xyz));
			// Add this hit to its side's average (by the hit normal).
			if ((b.x >> 24u) != 0u) {
				vec3 hit_normal = oct_to_vec3(unpackUnorm2x16(b.y) * 2.0 - 1.0);
				vec3 along = vec3(dot(hit_normal, axes[0]), dot(hit_normal, axes[1]), dot(hit_normal, axes[2]));
				int axis = abs(along.x) > abs(along.y) ? (abs(along.x) > abs(along.z) ? 0 : 2) : (abs(along.y) > abs(along.z) ? 1 : 2);
				uint bin = uint(object * 6 + axis * 2 + (along[axis] > 0.0 ? 1 : 0)) * 4u;
				uvec3 fixed_point = uvec3(clamp(leaving, vec3(0.0), vec3(64.0)) * NF_BIN_SCALE + 0.5);
				atomicAdd(nf_bins[bin], fixed_point.r);
				atomicAdd(nf_bins[bin + 1u], fixed_point.g);
				atomicAdd(nf_bins[bin + 2u], fixed_point.b);
				atomicAdd(nf_bins[bin + 3u], 1u);
			}
			float total = 0.0;
			if (nf_moving[object * 4 + 1].w > 0.5) {
				// Sphere: its sides blended by how much they face this block.
				vec3 facing = -object_dir;
				vec3 sum = vec3(0.0);
				float weight_sum = 0.0;
				for (int axis = 0; axis < 3; axis++) {
					float d = dot(facing, axes[axis]);
					uint bin = uint(object * 6 + axis * 2 + (d > 0.0 ? 1 : 0)) * 4u;
					uint count = nf_bins_previous[bin + 3u];
					vec3 side = count > 0u ? vec3(nf_bins_previous[bin], nf_bins_previous[bin + 1u], nf_bins_previous[bin + 2u]) / (NF_BIN_SCALE * float(count)) : leaving;
					sum += side * d * d;
					weight_sum += d * d;
				}
				object_light = sum / max(weight_sum, 1e-6) * weights.r;
				total = weights.r;
			} else {
				for (int axis = 0; axis < 3; axis++) {
					if (weights[axis] <= 0.0) {
						continue;
					}
					uint bin = uint(object * 6 + axis * 2 + int((sides >> uint(axis)) & 1u)) * 4u;
					uint count = nf_bins_previous[bin + 3u];
					vec3 side = count > 0u ? vec3(nf_bins_previous[bin], nf_bins_previous[bin + 1u], nf_bins_previous[bin + 2u]) / (NF_BIN_SCALE * float(count)) : leaving;
					object_light += side * weights[axis];
					total += weights[axis];
				}
			}
			// The light the object hides: what the probes (which don't see
			// it) give for a surface facing it. The block's own irradiance
			// averages the whole hemisphere and hid too little: under a ball,
			// the bright sky above is what it hides.
			vec4 hidden = ddgi_sample_irradiance(uintBitsToFloat(object_ray.xyz), object_dir, object_dir);
			object_light -= hidden.rgb * hidden.a * total;
		}
	}
	if (any(isnan(object_light)) || any(isinf(object_light))) {
		object_light = vec3(0.0);
	}
	imageStore(nf_object_out, block, vec4(object_light, 1.0));
	vec3 origin = uintBitsToFloat(r.xyz);
	vec3 dir = oct_to_vec3(unpackUnorm2x16(r.w) * 2.0 - 1.0);
	vec4 ray = texelFetch(sampler2D(nf_ray_data, linear_sampler), block, 0);
	// a: 2 for a hit on a moving object (MODE_NF_TEMPORAL lets go of the
	// history faster around it), 1 otherwise.
	float sample_flag = nf_bounce[idx].z != 0u ? 2.0 : 1.0;
	vec3 radiance;
	if (ray.a >= DDGI_MISS_DISTANCE * 0.5) {
		// Nothing within range: the probes' irradiance at the surface stands
		// for the light from farther away. Where no ray hits anything (open
		// ground under the sky) the result is the probes' light, without
		// noise. (The probes' irradiance at the ray's end, facing along the
		// ray, was tried first: its cosine lobe also takes in what lies to
		// the side, so rays along a sunlit floor brought its light back and
		// the floor came out too bright.)
		radiance = probe.rgb;
	} else {
		radiance = ray.rgb;
		uvec4 b = nf_bounce[idx];
		if ((b.x >> 24u) != 0u) {
			vec3 hit_normal = oct_to_vec3(unpackUnorm2x16(b.y) * 2.0 - 1.0);
			vec3 hit_pos = origin + dir * uintBitsToFloat(b.w);
			vec4 irradiance = ddgi_sample_irradiance(hit_pos, hit_normal, -dir);
			// A hit a few centimeters away (the other side of a corner): the
			// probe lookup there can reach probes on the far side of the
			// surface (a sunlit roof) and lit the corner as a bright streak.
			// Close by, this block's own probe light is the better estimate;
			// blend to the hit's over half a probe spacing.
			float near = 1.0 - smoothstep(0.0, 0.5 * ddgi.volumes[0].spacing.x, uintBitsToFloat(b.w));
			irradiance = mix(vec4(irradiance.rgb * irradiance.a, 1.0), vec4(probe.rgb, 1.0), near);
			vec3 feedback = unpackUnorm4x8(b.x).rgb * ddgi.schedule.w;
			if (ddgi.schedule.w > 1.0) {
				feedback = min(feedback, vec3(0.95));
			}
			radiance += feedback * irradiance.rgb * irradiance.a;
		}
	}
	// Clamp single bright rays (fireflies) without changing the hue, as the
	// probe blend does.
	float lum = ddgi_luminance(radiance);
	if (lum > ddgi.schedule.z) {
		radiance *= ddgi.schedule.z / lum;
	}
	if (any(isnan(radiance)) || any(isinf(radiance))) {
		radiance = vec3(0.0);
	}
	imageStore(nf_raw_out, block, vec4(radiance, sample_flag));
}

#endif // MODE_NF_RESOLVE

#ifdef MODE_NF_TEMPORAL

// The workgroup's 8x8 blocks and a border of 2, for the neighborhood
// statistics.
#define NF_RADIUS 2
#define NF_TILE (8 + 2 * NF_RADIUS)
shared vec4 tile_raw[NF_TILE * NF_TILE];
shared vec4 tile_geometry[NF_TILE * NF_TILE]; // Normal (xyz), depth (w; 0 for no surface).

void main() {
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	ivec2 tile_origin = ivec2(gl_WorkGroupID.xy) * 8 - NF_RADIUS;
	for (uint i = gl_LocalInvocationIndex; i < uint(NF_TILE * NF_TILE); i += 64u) {
		ivec2 c = tile_origin + ivec2(int(i) % NF_TILE, int(i) / NF_TILE);
		vec4 raw = vec4(0.0);
		vec4 geometry = vec4(0.0);
		if (all(greaterThanEqual(c, ivec2(0))) && all(lessThan(c, nf.size.xy))) {
			raw = texelFetch(sampler2D(nf_raw, linear_sampler), c, 0);
			float depth = imageLoad(low_data1, c).w;
			if (imageLoad(low_data0, c).a > 0.0 && depth > 0.0) {
				geometry = vec4(oct_to_vec3(vec2(imageLoad(low_data2, c).w, imageLoad(low_data3, c).w) * 2.0 - 1.0), depth);
			}
		}
		tile_raw[i] = raw;
		tile_geometry[i] = geometry;
	}
	barrier();

	if (any(greaterThanEqual(block, nf.size.xy))) {
		return;
	}
	ivec2 t = ivec2(gl_LocalInvocationID.xy) + NF_RADIUS;
	vec4 center_geometry = tile_geometry[t.y * NF_TILE + t.x];
	vec4 center_raw = tile_raw[t.y * NF_TILE + t.x];
	if (center_geometry.w <= 0.0 || center_raw.a <= 0.0) {
		imageStore(nf_history_out, block, vec4(0.0));
		imageStore(nf_geometry_out, block, vec4(0.0));
		imageStore(nf_noise_out, block, vec4(0.0));
		return;
	}
	float depth = center_geometry.w;
	vec3 normal = center_geometry.xyz;

	// Mean and variance of this frame's samples on the same surface nearby.
	vec3 sum = vec3(0.0);
	vec3 sum_sq = vec3(0.0);
	float weight_sum = 0.0;
	float weight_sq_sum = 0.0;
	// A ray nearby hit a moving object (a sample flag of 2), this frame or
	// the previous one. Only this frame's hits are passed on, or the flag
	// would keep itself alive through the history.
	bool moving_now = false;
	bool moving_before = false;
	for (int y = -NF_RADIUS; y <= NF_RADIUS; y++) {
		for (int x = -NF_RADIUS; x <= NF_RADIUS; x++) {
			int i = (t.y + y) * NF_TILE + t.x + x;
			vec4 g = tile_geometry[i];
			moving_now = moving_now || tile_raw[i].a > 1.5;
			float w = tile_raw[i].a > 0.0 ? nf_geometry_weight(depth, normal, g.w, g.xyz) : 0.0;
			sum += tile_raw[i].rgb * w;
			sum_sq += tile_raw[i].rgb * tile_raw[i].rgb * w;
			weight_sum += w;
			weight_sq_sum += w * w;
		}
	}
	vec3 mean = sum / weight_sum;
	vec3 deviation = sqrt(max(sum_sq / weight_sum - mean * mean, vec3(0.0)));
	float effective = weight_sum * weight_sum / max(weight_sq_sum, 1e-6);

	// The previous frame's accumulated light at this surface: reprojected
	// with the camera, bilinear over the blocks whose surface matches.
	vec4 history = vec4(0.0);
	float previous_probe = 0.0;
	if (nf.params.w > 0.5) {
		vec3 world = nf_block_world_position(block, depth);
		vec4 clip = nf.prev_view_projection * vec4(world, 1.0);
		if (clip.w > 1e-4) {
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			vec2 low_pos = uv * vec2(nf.size.xy) - 0.5;
			ivec2 base = ivec2(floor(low_pos));
			vec2 f = low_pos - vec2(base);
			float history_weight = 0.0;
			for (int k = 0; k < 4; k++) {
				ivec2 c = base + ivec2(k & 1, k >> 1);
				if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, nf.size.xy))) {
					continue;
				}
				vec4 g = texelFetch(sampler2D(nf_geometry, linear_sampler), c, 0);
				vec3 g_normal = oct_to_vec3(g.yz * 2.0 - 1.0);
				// The previous frame's depth of this point is clip.w.
				float w = nf_geometry_weight(clip.w, normal, abs(g.x), g_normal);
				vec2 bl = mix(1.0 - f, f, vec2(k & 1, k >> 1));
				w *= bl.x * bl.y;
				history += texelFetch(sampler2D(nf_history, linear_sampler), c, 0) * w;
				history_weight += w;
				// Moving objects were near here last frame (geometry w 2): the
				// light they left must go as fast as it came.
				moving_before = moving_before || (w > 0.0 && g.x < 0.0);
				previous_probe += g.w * w;
			}
			history = history_weight > 0.05 ? history / history_weight : vec4(0.0);
			previous_probe = history_weight > 0.05 ? previous_probe / history_weight : -1.0;
		}
	}

	bool moving = moving_now || moving_before;
	// Did the light here change? The probes (smooth, noise-free) tell: their
	// irradiance at the block is more than 5% off its slow moving average
	// (so gradual changes, a lamp moving away, add up too).
	vec3 probe_now = texelFetch(sampler2D(nf_probe, linear_sampler), block, 0).rgb;
	float probe_lum = ddgi_luminance(probe_now);
	bool light_changed = previous_probe < 0.0 || abs(probe_lum - previous_probe) > 0.05 * max(probe_lum, previous_probe) + 1e-4;
	float probe_average = previous_probe < 0.0 ? probe_lum : mix(previous_probe, probe_lum, 0.1);
	// The history holds the near-field correction (near-field light minus
	// the probes' light), not the light itself: when the probes change, the
	// result follows them at once, and only the correction is averaged over
	// many frames (with the light itself, the history trailed the probes
	// for about 2 seconds after a moving lamp stopped).
	vec3 sample_correction = center_raw.rgb - probe_now;
	vec3 result = sample_correction;
	float count = 1.0;
	if (history.a > 0.0) {
		history.rgb += probe_now;
		if (moving || light_changed) {
			// Keep the history within what this frame's neighborhood allows,
			// so a change shows within a few frames instead of trailing.
			// Near moving objects the clamp is tighter and the history lets
			// go faster.
			vec3 spread = (moving ? 2.0 : 4.0) * deviation / sqrt(max(effective, 1.0)) + (moving ? 0.01 : 0.03) * mean + 1e-4;
			history.rgb = clamp(history.rgb, mean - spread, mean + spread);
			count = min(history.a + 1.0, moving ? 8.0 : 16.0);
		} else {
			// Nothing changed: no clamp. In dark places a few rays reach a
			// bright opening and this frame's neighborhood jumps around; a
			// clamp to it passed that on as flicker (1.4% of the pixels in
			// a dark tunnel instead of 0.01%; a wide clamp, 6 standard
			// errors, still 0.02%).
			count = min(history.a + 1.0, 128.0);
		}
		float alpha = max(1.0 / count, (moving || light_changed) ? nf.params.z : 1.0 / 128.0);
		result = mix(history.rgb - probe_now, sample_correction, alpha);
	}
	imageStore(nf_history_out, block, vec4(result, count));
	// The standard error left in the accumulated light: this frame's spread
	// over the samples that went into it (the neighborhood, and the frames).
	float noise = ddgi_luminance(deviation) / sqrt(max(effective, 1.0) * count);
	imageStore(nf_noise_out, block, vec4(noise, 0.0, 0.0, 0.0));
	vec2 oct = vec3_to_oct(normal);
	imageStore(nf_geometry_out, block, vec4(moving_now ? -depth : depth, oct, probe_average));
}

#endif // MODE_NF_TEMPORAL

#ifdef MODE_NF_FILTER

// Two passes of an edge-aware 5x5 filter (a-trous): taps 1 block apart,
// then 2 apart, about 9x9 blocks in all. The workgroup's blocks and a
// border of 4 are loaded once.
#define NF_BORDER 4
#define NF_TILE (8 + 2 * NF_BORDER)
// How far above its noise (standard errors, after the filter) a difference
// from the probes must be to count fully.
#define NF_NOISE_THRESHOLD 1.0
shared vec4 tile_light[NF_TILE * NF_TILE];
shared vec4 tile_geometry[NF_TILE * NF_TILE];

void main() {
	bool final_pass = (params.flags & FLAG_NF_FILTER_FINAL) != 0u;
	int tap_step = final_pass ? 2 : 1;
	ivec2 block = ivec2(gl_GlobalInvocationID.xy);
	ivec2 tile_origin = ivec2(gl_WorkGroupID.xy) * 8 - NF_BORDER;
	for (uint i = gl_LocalInvocationIndex; i < uint(NF_TILE * NF_TILE); i += 64u) {
		ivec2 c = tile_origin + ivec2(int(i) % NF_TILE, int(i) / NF_TILE);
		vec4 light = vec4(0.0);
		vec4 geometry = vec4(0.0);
		if (all(greaterThanEqual(c, ivec2(0))) && all(lessThan(c, nf.size.xy))) {
			light = texelFetch(sampler2D(nf_history, linear_sampler), c, 0);
			if (!final_pass) {
				// The history holds the correction to the probes' light.
				light.rgb += texelFetch(sampler2D(nf_probe, linear_sampler), c, 0).rgb;
			}
			float depth = imageLoad(low_data1, c).w;
			if (light.a > 0.0 && depth > 0.0) {
				geometry = vec4(oct_to_vec3(vec2(imageLoad(low_data2, c).w, imageLoad(low_data3, c).w) * 2.0 - 1.0), depth);
			}
		}
		tile_light[i] = light;
		tile_geometry[i] = geometry;
	}
	barrier();

	if (any(greaterThanEqual(block, nf.size.xy))) {
		return;
	}
	ivec2 t = ivec2(gl_LocalInvocationID.xy) + NF_BORDER;
	vec4 center = tile_geometry[t.y * NF_TILE + t.x];
	if (center.w <= 0.0) {
		if (!final_pass) {
			imageStore(nf_filter_out, block, tile_light[t.y * NF_TILE + t.x]);
		}
		return; // The final pass leaves the probes' light.
	}
	vec3 sum = vec3(0.0);
	float weight_sum = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			int i = (t.y + y * tap_step) * NF_TILE + t.x + x * tap_step;
			vec4 g = tile_geometry[i];
			float w = nf_geometry_weight(center.w, center.xyz, g.w, g.xyz) * exp(-0.35 * float(x * x + y * y));
			sum += tile_light[i].rgb * w;
			weight_sum += w;
		}
	}
	vec3 light = sum / max(weight_sum, 1e-6);
	if (!final_pass) {
		imageStore(nf_filter_out, block, vec4(light, tile_light[t.y * NF_TILE + t.x].a));
		return;
	}

	// In place of the probes' light; the gradients over the normal (from
	// the probes) are scaled by the same ratio.
	vec4 probe = texelFetch(sampler2D(nf_probe, linear_sampler), block, 0);
	// Only where the near-field light differs from the probes' by more than
	// its remaining noise: in a dark tunnel a few rays reach the sunlit
	// ground outside, and their average wanders around the probes' (smooth)
	// value as blotches. Real near-field light (a contact shadow, color
	// bleeding) differs by much more and stays.
	vec3 difference = light - probe.rgb;
	float d = max(abs(difference.r), max(abs(difference.g), abs(difference.b)));
	float noise = texelFetch(sampler2D(nf_noise, linear_sampler), block, 0).r * NF_NOISE_THRESHOLD;
	light = probe.rgb + difference * (d * d / (d * d + noise * noise + 1e-12));
	// The moving objects' light (the same every frame, averaged per side of
	// the object): not accumulated, not filtered.
	light = max(light + texelFetch(sampler2D(nf_object, linear_sampler), block, 0).rgb, vec3(0.0));
	float energy = ddgi.volumes[0].params.x;
	vec4 d0 = imageLoad(low_data0, block);
	d0.rgb = max(d0.rgb + (light - probe.rgb) * energy, vec3(0.0));
	vec3 ratio = clamp(light / max(probe.rgb, vec3(1e-4)), vec3(0.0), vec3(4.0));
	vec4 d1 = imageLoad(low_data1, block);
	vec4 d2 = imageLoad(low_data2, block);
	vec4 d3 = imageLoad(low_data3, block);
	d1.xyz *= ratio.r;
	d2.xyz *= ratio.g;
	d3.xyz *= ratio.b;
	imageStore(low_data0, block, d0);
	imageStore(low_data1, block, d1);
	imageStore(low_data2, block, d2);
	imageStore(low_data3, block, d3);
}

#endif // MODE_NF_FILTER
