#[compute]

#version 450

#VERSION_DEFINES

// Planar reflections by pixel projection, after A. Cichocki, "Optimized pixel-projected
// reflections for planar reflectors", SIGGRAPH 2017 Advances in Real-Time Rendering.
// Written from the talk; no code of it is used.
//
// Instead of marching rays through the depth buffer (SSR), every pixel is mirrored
// across each reflector plane and projected back onto the screen: the pixel where it
// lands sees it in the mirror. MODE_PROJECT scatters, per pixel, the screen offset to
// the place it is reflected into an intermediate buffer; where several pixels land on
// the same place, the smallest offset (the one nearest to the reflector) wins through
// an atomic max on the inverted offset. MODE_RESOLVE reads the offset back, fills the
// small holes left where the reflection is stretched, and fetches the reflected color
// from the last frame (reprojected), blurred by the surface roughness.
//
// Reflectors are rectangles. A reflector pixel needs no depth of its own: it is where
// the camera ray meets the rectangle in front of the depth buffer, so transparent
// reflectors (water) work as well as opaque ones (floors, mirrors).
//
// Mirror pass: one reflector per view may also have been rendered from the camera
// reflected across it (RendererSceneCull::_render_mirror()), with the same projection,
// so its color at a pixel's screen position is what that pixel reflects. It fills
// what the projection can't see (off screen, behind the camera, hidden sides),
// blurred by roughness times the distance behind the mirror (contact hardening).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define MAX_REFLECTORS 8

struct Reflector {
	vec4 plane; // View space normal (pointing to the reflected side), w = dot(normal, point on plane).
	vec4 center; // View space center of the rectangle, w = intensity.
	vec4 axis_u; // View space unit axis, w = half extent.
	vec4 axis_v; // View space unit axis, w = half extent.
	vec4 params; // x = thickness, y = normal distortion, z = roughness cutoff, w = unused.
};

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 projection; // View to clip (depth corrected).
	mat4 inv_projection;
	mat4 reprojection; // Clip of this frame to clip of the last frame (for the color lookup).
	ivec2 screen_size; // Size of the depth buffer.
	ivec2 target_size; // Size of the intermediate and output buffers (full or half).
	uint reflector_count;
	float max_color_lod;
	uint orthogonal;
	uint history_valid;
	int mirror_reflector; // Reflector with a mirror pass, -1 for none.
	float mirror_max_lod;
	uint pad0;
	uint pad1;
	vec4 mirror_rect; // Screen UV rectangle the mirror pass covers (position, size).
	Reflector reflectors[MAX_REFLECTORS];
}
params;

#if defined(MODE_PROJECT) || defined(MODE_RESOLVE)
layout(set = 0, binding = 1) uniform sampler2D source_depth;
layout(r32ui, set = 0, binding = 2) uniform restrict uimage2D intermediate;
layout(set = 0, binding = 3) uniform sampler2D source_normal_roughness;
layout(set = 0, binding = 4) uniform sampler2D source_last_frame;
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D output_color; // This frame's reflection.
layout(rgba16f, set = 0, binding = 6) uniform restrict writeonly image2D output_params;
layout(rg16f, set = 0, binding = 7) uniform restrict writeonly image2D output_motion; // To last frame's position of the reflection, in UV.
layout(set = 0, binding = 8) uniform sampler2D mirror_color; // Mirror pass color (linear HDR), mipmapped.
layout(set = 0, binding = 9) uniform sampler2D mirror_depth;
#endif

#ifdef MODE_TEMPORAL
layout(set = 0, binding = 1) uniform sampler2D current_color;
layout(set = 0, binding = 2) uniform sampler2D motion;
layout(set = 0, binding = 3) uniform sampler2D history;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D output_color;
#endif

// Sky pixels (reverse Z: depth 0) are treated as points this far away.
#define SKY_DISTANCE 10000.0

vec3 view_from_uv_depth(vec2 uv, float depth) {
	vec4 pos = params.inv_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	return pos.xyz / pos.w;
}

#if defined(MODE_PROJECT) || defined(MODE_RESOLVE)
vec3 view_position(vec2 uv) {
	float depth = textureLod(source_depth, uv, 0.0).r;
	if (depth <= 0.0 && params.orthogonal == 0) {
		// Sky: a far point along the view ray.
		vec3 dir = normalize(view_from_uv_depth(uv, 0.5));
		return dir * SKY_DISTANCE;
	}
	return view_from_uv_depth(uv, depth);
}
#endif

// Returns the screen position (in target pixels) of a view space point, z < 0 in front.
bool project_to_target(vec3 p, out vec2 r_pixel) {
	vec4 clip = params.projection * vec4(p, 1.0);
	if (clip.w <= 0.0) {
		return false;
	}
	vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
	r_pixel = uv * vec2(params.target_size);
	return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)));
}

// Where the ray from the eye through p meets the reflector's plane (in front of the
// eye), if inside its rectangle.
bool ray_hits_reflector(uint i, vec3 eye, vec3 p, out vec3 r_hit, out float r_t) {
	Reflector r = params.reflectors[i];
	vec3 dir = p - eye;
	float denom = dot(r.plane.xyz, dir);
	if (abs(denom) < 1e-8) {
		return false;
	}
	float t = (r.plane.w - dot(r.plane.xyz, eye)) / denom;
	if (t <= 0.0) {
		return false;
	}
	r_t = t;
	r_hit = eye + dir * t;
	vec3 local = r_hit - r.center.xyz;
	return abs(dot(local, r.axis_u.xyz)) <= r.axis_u.w && abs(dot(local, r.axis_v.xyz)) <= r.axis_v.w;
}

vec3 camera_eye(vec3 p) {
	// Perspective: the eye is the origin. Orthogonal: the ray is parallel to -Z.
	return params.orthogonal != 0 ? vec3(p.xy, 0.0) : vec3(0.0);
}

// Whether another reflector, facing the camera, lies in front of reflector i's point
// hit on the ray from the eye (water over a reflective floor, overlapping mirrors).
bool covered_by_other_reflector(uint i, vec3 eye, vec3 hit) {
	for (uint j = 0; j < params.reflector_count; j++) {
		if (j == i || dot(params.reflectors[j].plane.xyz, eye) - params.reflectors[j].plane.w <= 0.0) {
			continue;
		}
		vec3 other_hit;
		float t;
		if (ray_hits_reflector(j, eye, hit, other_hit, t) && t < 0.999) {
			return true;
		}
	}
	return false;
}

// How far a point must be from a reflector's plane to be reflected by it: points
// closer belong to the reflecting surface itself (its thickness covers waves).
float surface_tolerance(Reflector r, float view_depth) {
	return r.params.x + 0.002 * view_depth + 0.01;
}

// Offset encoding (32 bits; larger stored value = smaller offset):
// the offset is expressed in the axis frame (+x, -x, +y, -y) closest to its direction,
// so that its component along that axis is positive and grows with the distance. That
// component goes to the high bits (12.3 fixed point), the perpendicular one (signed 12.3)
// and the frame index below, so comparing values compares distances.
uint encode_offset(vec2 offset) {
	uint frame;
	vec2 local; // x: perpendicular, y: along the frame axis (>= 0).
	if (abs(offset.x) >= abs(offset.y)) {
		frame = offset.x >= 0.0 ? 0u : 1u;
		local = vec2(offset.y, abs(offset.x));
	} else {
		frame = offset.y >= 0.0 ? 2u : 3u;
		local = vec2(offset.x, abs(offset.y));
	}
	uint along = uint(clamp(round(local.y * 8.0), 0.0, 32767.0));
	uint across = uint(clamp(round(local.x * 8.0) + 16384.0, 0.0, 32767.0));
	uint key = (along << 17u) | (across << 2u) | frame;
	return ~key;
}

vec2 decode_offset(uint stored) {
	uint key = ~stored;
	uint frame = key & 3u;
	float across = float((key >> 2u) & 0x7FFFu) / 8.0 - 2048.0;
	float along = float(key >> 17u) / 8.0;
	if (frame == 0u) {
		return vec2(along, across);
	} else if (frame == 1u) {
		return vec2(-along, across);
	} else if (frame == 2u) {
		return vec2(across, along);
	}
	return vec2(across, -along);
}

#ifdef MODE_PROJECT

// Where a view space point is seen in reflector i, in target pixels, if it is.
bool reflect_to_target(uint i, vec3 p, out vec2 r_dest) {
	Reflector r = params.reflectors[i];
	float height = dot(r.plane.xyz, p) - r.plane.w;
	if (height <= surface_tolerance(r, -p.z)) {
		return false; // Behind the mirror or part of its surface: not reflected by it.
	}
	vec3 mirrored = p - 2.0 * height * r.plane.xyz;
	vec3 eye = camera_eye(mirrored);
	if (dot(r.plane.xyz, eye) - r.plane.w <= 0.0) {
		return false; // Mirror seen from behind.
	}
	vec3 hit;
	float t;
	if (!ray_hits_reflector(i, eye, mirrored, hit, t) || covered_by_other_reflector(i, eye, hit)) {
		return false;
	}
	if (!project_to_target(hit, r_dest)) {
		return false;
	}
	// The reflector must be visible there (not behind the depth buffer).
	float scene_depth = -view_position(r_dest / vec2(params.target_size)).z;
	float plane_depth = -hit.z;
	return scene_depth >= plane_depth - surface_tolerance(r, plane_depth);
}

void main() {
	ivec2 target_pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(target_pos, params.target_size))) {
		return;
	}

	vec2 source_px = vec2(target_pos) + 0.5;
	vec3 p = view_position(source_px / vec2(params.target_size));

	// The right and lower neighbors give the footprint of this pixel in the
	// reflection: where the reflection magnifies (grazing views, mirrors seen from
	// close by), one pixel covers many, and writing only one leaves a grid of gaps.
	vec3 right = view_position((source_px + vec2(1.0, 0.0)) / vec2(params.target_size));
	vec3 down = view_position((source_px + vec2(0.0, 1.0)) / vec2(params.target_size));
	// Same surface: no depth edge in between (else the footprint would stretch the
	// foreground over the background).
	float edge_tolerance = 0.03 * (-p.z) + 0.02;
	bool right_ok = length(right - p) < edge_tolerance;
	bool down_ok = length(down - p) < edge_tolerance;

	for (uint i = 0; i < params.reflector_count; i++) {
		vec2 dest;
		if (!reflect_to_target(i, p, dest)) {
			continue;
		}
		vec2 dest_right = dest;
		vec2 dest_down = dest;
		bool has_right = right_ok && reflect_to_target(i, right, dest_right);
		bool has_down = down_ok && reflect_to_target(i, down, dest_down);
		vec2 du = has_right ? dest_right - dest : vec2(0.0);
		vec2 dv = has_down ? dest_down - dest : vec2(0.0);

		// Fill the parallelogram spanned by the two neighbors' spots (at most 24
		// pixels per side). Each pixel gets its own offset, to the matching point
		// between the source pixels, so the nearest-wins rule still holds.
		int steps_u = int(clamp(ceil(length(du)), 1.0, 24.0));
		int steps_v = int(clamp(ceil(length(dv)), 1.0, 24.0));
		for (int b = 0; b < steps_v; b++) {
			for (int a = 0; a < steps_u; a++) {
				vec2 f = vec2(float(a) / float(steps_u), float(b) / float(steps_v));
				vec2 d = dest + du * f.x + dv * f.y;
				ivec2 di = ivec2(d);
				if (all(greaterThanEqual(di, ivec2(0))) && all(lessThan(di, params.target_size))) {
					imageAtomicMax(intermediate, di, encode_offset(d - (source_px + f)));
				}
			}
		}
	}
}

#endif // MODE_PROJECT

#ifdef MODE_RESOLVE

vec3 rgb_to_ycocg(vec3 c) {
	return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 ycocg_to_rgb(vec3 c) {
	return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float decode_roughness(float w) {
	if (w > 0.5) {
		w = 1.0 - w;
	}
	return w / (127.0 / 255.0);
}

// Last frame's color at this frame's screen position (uv) of a view space point.
vec3 last_frame_color(vec2 uv, float depth, float lod) {
	vec4 prev = params.reprojection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec2 prev_uv = prev.w > 0.0 ? prev.xy / prev.w * 0.5 + 0.5 : uv;
	return textureLod(source_last_frame, prev_uv, lod).rgb;
}

void main() {
	ivec2 target_pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(target_pos, params.target_size))) {
		return;
	}

	vec2 target_px = vec2(target_pos) + 0.5;
	vec2 uv = target_px / vec2(params.target_size);
	imageStore(output_motion, target_pos, vec4(0.0));

	// Which reflector this pixel shows: the nearest rectangle the camera ray meets in
	// front of the depth buffer.
	vec3 far_point = view_from_uv_depth(uv, 0.5);
	vec3 eye = camera_eye(far_point);
	float scene_depth = -view_position(uv).z;
	int reflector = -1;
	float plane_depth = 1e20;
	vec3 plane_hit = vec3(0.0);
	for (uint i = 0; i < params.reflector_count; i++) {
		vec3 hit;
		float t;
		if (!ray_hits_reflector(i, eye, far_point, hit, t)) {
			continue;
		}
		Reflector r = params.reflectors[i];
		float d = -hit.z;
		if (dot(r.plane.xyz, eye) - r.plane.w <= 0.0) {
			continue; // Seen from behind.
		}
		if (scene_depth < d - surface_tolerance(r, d) || d >= plane_depth) {
			continue;
		}
		reflector = int(i);
		plane_depth = d;
		plane_hit = hit;
	}

	if (reflector < 0) {
		imageStore(output_color, target_pos, vec4(0.0));
		imageStore(output_params, target_pos, vec4(0.0));
		return;
	}

	Reflector r = params.reflectors[reflector];

	// An opaque surface at the plane's depth must also face like the plane: the
	// bottom of an object standing on a reflective floor is within the depth
	// tolerance, but it isn't the floor.
	if (abs(scene_depth - plane_depth) <= surface_tolerance(r, plane_depth)) {
		vec3 n = normalize(textureLod(source_normal_roughness, uv, 0.0).xyz * 2.0 - 1.0);
		if (dot(n, r.plane.xyz) < 0.7) {
			imageStore(output_color, target_pos, vec4(0.0));
			imageStore(output_params, target_pos, vec4(0.0));
			return;
		}
	}

	imageStore(output_params, target_pos, vec4(plane_depth, r.params.x, r.params.y, r.params.z));

	// The mirror pass, where there is one: what this pixel reflects, at its own
	// screen position. Premultiplied by the reflector's intensity.
	vec4 mirror = vec4(0.0);
	vec2 mirror_motion = vec2(2.0); // Off screen: no history.
	float mirror_virtual_distance = 0.0; // From the eye to the mirror image of what this pixel reflects.
	vec3 mirror_virtual_dir = vec3(0.0, 0.0, -1.0);
	vec2 mirror_uv = (uv - params.mirror_rect.xy) / params.mirror_rect.zw;
	if (reflector == params.mirror_reflector && all(greaterThanEqual(mirror_uv, vec2(0.0))) && all(lessThanEqual(mirror_uv, vec2(1.0)))) {
		// Same depth as the full projection would give: the crop only scales x and y.
		float md = textureLod(mirror_depth, mirror_uv, 0.0).r;
		// In the mirror camera's view space, what it sees is where the main camera
		// sees its mirror image.
		vec3 virtual_pos = (md <= 0.0 && params.orthogonal == 0) ? normalize(view_from_uv_depth(uv, 0.5)) * SKY_DISTANCE : view_from_uv_depth(uv, md);
		float total = length(virtual_pos - eye);
		mirror_virtual_distance = total;
		mirror_virtual_dir = (virtual_pos - eye) / max(total, 1e-6);
		float behind = max(total - length(plane_hit - eye), 0.0); // From the mirror to what it reflects.
		// The reflection cone (GGX alpha as its half-width slope) spreads over
		// 2 alpha behind; seen from total away, that many mirror texels.
		float alpha = decode_roughness(textureLod(source_normal_roughness, uv, 0.0).w);
		alpha *= alpha;
		float texels = 2.0 * alpha * behind / max(total, 1e-3) * abs(params.projection[1][1]) * 0.5 * float(textureSize(mirror_color, 0).y) / params.mirror_rect.w;
		float lod = clamp(log2(max(texels, 1.0)), 0.0, params.mirror_max_lod);
		float w = clamp(r.center.w, 0.0, 1.0);
		mirror = vec4(textureLod(mirror_color, mirror_uv, lod).rgb * w, w);
		vec4 prev = params.reprojection * vec4(uv * 2.0 - 1.0, md, 1.0);
		if (prev.w > 0.0) {
			mirror_motion = prev.xy / prev.w * 0.5 + 0.5 - uv;
		}
	}

	// Offset of the reflected pixel; fill holes from the 4 neighbors: take a neighbor's
	// offset where there is none, or where a neighbor's is clearly shorter (nearer
	// geometry stretched over this pixel).
	uint stored = imageLoad(intermediate, target_pos).r;
	const ivec2 neighbors[4] = ivec2[](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
	uint best = 0u;
	for (int n = 0; n < 4; n++) {
		ivec2 q = clamp(target_pos + neighbors[n], ivec2(0), params.target_size - 1);
		best = max(best, imageLoad(intermediate, q).r);
	}
	if (best != 0u) {
		if (stored == 0u) {
			stored = best;
		} else {
			vec2 own = decode_offset(stored);
			vec2 other = decode_offset(best);
			if (length(own) - length(other) > 6.0) {
				stored = best;
			}
		}
	}

	vec2 offset = stored != 0u ? decode_offset(stored) : vec2(0.0);
	bool bridged = false;

	if (stored == 0u) {
		// At grazing angles the reflection is stretched along the reflected ray's
		// direction on screen, leaving gaps of several pixels between the rows that
		// were written. Bridge a gap only where reflected data lies on both sides of
		// it with similar offsets, so nothing is extended past an object's edge.
		vec3 refl_dir = reflect(normalize(plane_hit - eye), r.plane.xyz);
		vec4 clip = params.projection * vec4(plane_hit + refl_dir * max(0.25 * plane_depth, 0.5), 1.0);
		if (clip.w > 0.0) {
			vec2 dir = (clip.xy / clip.w * 0.5 + 0.5) * vec2(params.target_size) - target_px;
			if (length(dir) > 1e-3) {
				dir = normalize(dir);
				uint before = 0u;
				uint after = 0u;
				float before_k = 0.0;
				float after_k = 0.0;
				// 1 to 8 pixels one by one, then wider steps up to 32 (very grazing views).
				for (int i = 1; i <= 14; i++) {
					float k = i <= 8 ? float(i) : 8.0 + float(i - 8) * 4.0;
					if (before == 0u) {
						ivec2 q = clamp(ivec2(target_px - dir * k), ivec2(0), params.target_size - 1);
						before = imageLoad(intermediate, q).r;
						before_k = k;
					}
					if (after == 0u) {
						ivec2 q = clamp(ivec2(target_px + dir * k), ivec2(0), params.target_size - 1);
						after = imageLoad(intermediate, q).r;
						after_k = k;
					}
				}
				if (before != 0u && after != 0u) {
					vec2 ob = decode_offset(before);
					vec2 oa = decode_offset(after);
					if (abs(length(ob) - length(oa)) < 0.2 * max(length(ob), length(oa)) + 4.0) {
						offset = before_k <= after_k ? ob : oa;
						stored = before_k <= after_k ? before : after;
						bridged = true;
					}
				}
			}
		}
	}

	if (stored == 0u) {
		// Nothing on screen is reflected here (it is off screen or behind the camera):
		// the mirror pass, else the scene shader falls back to probes and the sky.
		imageStore(output_color, target_pos, mirror);
		imageStore(output_motion, target_pos, vec4(mirror_motion, 0.0, 0.0));
		return;
	}

	vec2 source_px = target_px - offset; // Sub-pixel position of the reflected point.
	vec2 source_uv = source_px / vec2(params.target_size);
	vec2 source_uv_point = (floor(source_px) + 0.5) / vec2(params.target_size);
	if (any(lessThan(source_uv, vec2(0.0))) || any(greaterThan(source_uv, vec2(1.0)))) {
		imageStore(output_color, target_pos, mirror);
		imageStore(output_motion, target_pos, vec4(mirror_motion, 0.0, 0.0));
		return;
	}

	// Rough surfaces blur the reflection, more the farther the reflected point is.
	vec2 nr_uv = (vec2(target_pos) + 0.5) / vec2(params.target_size);
	float roughness = decode_roughness(textureLod(source_normal_roughness, nr_uv, 0.0).w);
	float distance_px = length(offset) * float(params.screen_size.x) / float(params.target_size.x);
	float lod = clamp(log2(1.0 + roughness * roughness * distance_px * 0.25), 0.0, params.max_color_lod);

	float source_depth = textureLod(source_depth, source_uv_point, 0.0).r;
	vec3 filtered = last_frame_color(source_uv, source_depth, lod);
	vec3 point = last_frame_color(source_uv_point, source_depth, lod);

	// Bilinear filtering of the stretched image bleeds colors across edges in the
	// reflection: keep luminance and chroma near the point sample where they differ a lot.
	vec3 yf = rgb_to_ycocg(filtered);
	vec3 yp = rgb_to_ycocg(point);
	float luma_scale = max(max(yf.x, yp.x), 1e-4);
	float luma_mix = clamp(3.0 * abs(yf.x - yp.x) / luma_scale, 0.0, 1.0);
	vec2 chroma_f = yf.yz / max(yf.x, 1e-4);
	vec2 chroma_p = yp.yz / max(yp.x, 1e-4);
	float chroma_mix = clamp(1.4 * length(chroma_f - chroma_p), 0.0, 1.0);
	float y = mix(yf.x, yp.x, luma_mix);
	vec3 color = ycocg_to_rgb(vec3(y, mix(chroma_f, chroma_p, chroma_mix) * y));
	color = max(color, vec3(0.0));

	// Fade toward the screen edges, where the reflected content ends, over 8% of
	// the screen so the change to the probes' reflection doesn't show as a seam.
	vec2 edge = min(source_uv, 1.0 - source_uv);
	float edge_fade = smoothstep(0.0, 0.08, min(edge.x, edge.y));

	// Coverage: where only some neighbors have reflected data (the edge of a
	// reflected object, partly filled holes), blend with the probes' reflection by
	// how many do, instead of switching per pixel (which sparkles).
	// Counted along the row and the column separately: a stretched reflection
	// arrives as full rows (or columns) with gaps between them, which must not fade.
	float covered_x = 0.0;
	float covered_y = 0.0;
	for (int k = -1; k <= 1; k++) {
		covered_x += imageLoad(intermediate, clamp(target_pos + ivec2(k, 0), ivec2(0), params.target_size - 1)).r != 0u ? 1.0 : 0.0;
		covered_y += imageLoad(intermediate, clamp(target_pos + ivec2(0, k), ivec2(0), params.target_size - 1)).r != 0u ? 1.0 : 0.0;
	}
	float coverage = bridged ? 1.0 : smoothstep(1.0, 3.0, max(covered_x, covered_y)); // Bridged gaps have data on both sides by construction.

	float projected = edge_fade * coverage; // How much the projection is trusted here; the mirror pass (or the probes) fills the rest.

	if (mirror.a > 0.0 && mirror_virtual_distance > 0.0) {
		// Magnified projection (a mirror seen from close by at a grazing angle: one
		// source pixel stretched over several, blocky edges): use the mirror pass
		// where it has more pixels per screen pixel than the projection's source.
		// A surface seen directly at distance Dd and angle cos_d, and in the mirror
		// at Dv and cos_v, is magnified about (Dd / Dv) (cos_v / cos_d) along its
		// most foreshortened axis: smooth from pixel to pixel, unlike neighbor offsets.
		vec3 source_pos = view_position(source_uv_point);
		vec3 source_normal = normalize(textureLod(source_normal_roughness, source_uv_point, 0.0).xyz * 2.0 - 1.0);
		vec3 virtual_normal = source_normal - 2.0 * dot(source_normal, r.plane.xyz) * r.plane.xyz;
		float cos_direct = max(abs(dot(source_normal, normalize(source_pos - eye))), 0.05);
		float cos_virtual = max(abs(dot(virtual_normal, mirror_virtual_dir)), 0.05);
		float source_step = (mirror_virtual_distance / max(length(source_pos - eye), 1e-3)) * (cos_direct / cos_virtual); // Source pixels per target pixel.
		float mirror_step = float(textureSize(mirror_color, 0).y) / (float(params.target_size.y) * params.mirror_rect.w);
		projected *= smoothstep(0.6, 1.0, source_step / max(mirror_step, 1e-3));
	}
	float weight = clamp(r.center.w, 0.0, 1.0) * projected;

	imageStore(output_color, target_pos, vec4(color * weight, weight) + mirror * (1.0 - projected)); // Premultiplied.
	if (projected < 0.5 && mirror.a > 0.0) {
		imageStore(output_motion, target_pos, vec4(mirror_motion, 0.0, 0.0));
		return;
	}

	// Where this reflection was last frame: a reflection is seen where the camera
	// sees the mirror image of the reflected point, which stays put when only the
	// camera moves; so project the mirror image with last frame's camera.
	vec3 reflected = view_position(source_uv_point);
	vec3 mirrored = reflected - 2.0 * (dot(r.plane.xyz, reflected) - r.plane.w) * r.plane.xyz;
	vec4 clip = params.projection * vec4(mirrored, 1.0);
	if (clip.w > 0.0) {
		vec3 ndc = clip.xyz / clip.w;
		vec4 prev = params.reprojection * vec4(ndc, 1.0);
		if (prev.w > 0.0) {
			imageStore(output_motion, target_pos, vec4(prev.xy / prev.w * 0.5 + 0.5 - (ndc.xy * 0.5 + 0.5), 0.0, 0.0));
		}
	}
}

#endif // MODE_RESOLVE

#ifdef MODE_TEMPORAL

// Accumulates the reflection over frames: the projection picks slightly different
// pixels every frame (sub-pixel positions, stretched areas), which shimmers while
// the camera moves. Last frame's result is fetched where the reflection was and
// clamped to this frame's 3x3 neighborhood, so moving objects don't leave trails.
void main() {
	ivec2 target_pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(target_pos, params.target_size))) {
		return;
	}
	vec4 current = texelFetch(current_color, target_pos, 0);
	vec4 low = current;
	vec4 high = current;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			vec4 c = texelFetch(current_color, clamp(target_pos + ivec2(x, y), ivec2(0), params.target_size - 1), 0);
			low = min(low, c);
			high = max(high, c);
		}
	}
	vec2 uv = (vec2(target_pos) + 0.5) / vec2(params.target_size);
	vec2 prev_uv = uv + texelFetch(motion, target_pos, 0).xy;
	vec4 result = current;
	if (params.history_valid != 0u && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
		vec4 previous = clamp(textureLod(history, prev_uv, 0.0), low, high);
		result = mix(previous, current, 0.15);
	}
	imageStore(output_color, target_pos, result);
}

#endif // MODE_TEMPORAL
