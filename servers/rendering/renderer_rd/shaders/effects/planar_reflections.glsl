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
	uint pad;
	Reflector reflectors[MAX_REFLECTORS];
}
params;

layout(set = 0, binding = 1) uniform sampler2D source_depth;
layout(r32ui, set = 0, binding = 2) uniform restrict uimage2D intermediate;
layout(set = 0, binding = 3) uniform sampler2D source_normal_roughness;
layout(set = 0, binding = 4) uniform sampler2D source_last_frame;
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D output_color;
layout(rgba16f, set = 0, binding = 6) uniform restrict writeonly image2D output_params;

// Sky pixels (reverse Z: depth 0) are treated as points this far away.
#define SKY_DISTANCE 10000.0

vec3 view_from_uv_depth(vec2 uv, float depth) {
	vec4 pos = params.inv_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	return pos.xyz / pos.w;
}

vec3 view_position(vec2 uv) {
	float depth = textureLod(source_depth, uv, 0.0).r;
	if (depth <= 0.0 && params.orthogonal == 0) {
		// Sky: a far point along the view ray.
		vec3 dir = normalize(view_from_uv_depth(uv, 0.5));
		return dir * SKY_DISTANCE;
	}
	return view_from_uv_depth(uv, depth);
}

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

void main() {
	ivec2 target_pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(target_pos, params.target_size))) {
		return;
	}

	vec2 uv = (vec2(target_pos) + 0.5) / vec2(params.target_size);
	vec3 p = view_position(uv);

	for (uint i = 0; i < params.reflector_count; i++) {
		Reflector r = params.reflectors[i];
		float height = dot(r.plane.xyz, p) - r.plane.w;
		if (height <= surface_tolerance(r, -p.z)) {
			continue; // Behind the mirror or part of its surface: not reflected by it.
		}
		vec3 mirrored = p - 2.0 * height * r.plane.xyz;
		vec3 eye = camera_eye(mirrored);
		if (dot(r.plane.xyz, eye) - r.plane.w <= 0.0) {
			continue; // Mirror seen from behind.
		}
		vec3 hit;
		float t;
		if (!ray_hits_reflector(i, eye, mirrored, hit, t) || covered_by_other_reflector(i, eye, hit)) {
			continue;
		}
		vec2 dest;
		if (!project_to_target(hit, dest)) {
			continue;
		}
		// The reflector must be visible there (not behind the depth buffer).
		vec2 dest_uv = dest / vec2(params.target_size);
		float scene_depth = -view_position(dest_uv).z;
		float plane_depth = -hit.z;
		if (scene_depth < plane_depth - surface_tolerance(r, plane_depth)) {
			continue;
		}
		vec2 offset = dest - (vec2(target_pos) + 0.5);
		imageAtomicMax(intermediate, ivec2(dest), encode_offset(offset));
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

	// Which reflector this pixel shows: the nearest rectangle the camera ray meets in
	// front of the depth buffer.
	vec3 far_point = view_from_uv_depth(uv, 0.5);
	vec3 eye = camera_eye(far_point);
	float scene_depth = -view_position(uv).z;
	int reflector = -1;
	float plane_depth = 1e20;
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
	}

	if (reflector < 0) {
		imageStore(output_color, target_pos, vec4(0.0));
		imageStore(output_params, target_pos, vec4(0.0));
		return;
	}

	Reflector r = params.reflectors[reflector];
	imageStore(output_params, target_pos, vec4(plane_depth, r.params.x, r.params.y, r.params.z));

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

	if (stored == 0u) {
		// Nothing on screen is reflected here (it is off screen or behind the camera):
		// the scene shader falls back to probes and the sky.
		imageStore(output_color, target_pos, vec4(0.0));
		return;
	}

	vec2 offset = decode_offset(stored);
	vec2 source_px = target_px - offset; // Sub-pixel position of the reflected point.
	vec2 source_uv = source_px / vec2(params.target_size);
	vec2 source_uv_point = (floor(source_px) + 0.5) / vec2(params.target_size);
	if (any(lessThan(source_uv, vec2(0.0))) || any(greaterThan(source_uv, vec2(1.0)))) {
		imageStore(output_color, target_pos, vec4(0.0));
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

	// Fade toward the screen edges, where the reflected content ends.
	vec2 edge = min(source_uv, 1.0 - source_uv);
	float edge_fade = clamp(min(edge.x, edge.y) * 20.0, 0.0, 1.0);
	float weight = clamp(r.center.w, 0.0, 1.0) * edge_fade;

	imageStore(output_color, target_pos, vec4(color * weight, weight)); // Premultiplied.
}

#endif // MODE_RESOLVE
