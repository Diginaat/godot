/**************************************************************************/
/*  wind_shader_include.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// The wind shader include, served from memory at
// WindServer::get_shader_include_path(). wind_noise() and wind_sample() must
// stay identical to WindServer::noise(), WindServer::sample_state() and
// WindServer::apply_zone().
static const char *wind_shader_include_code = R"(// Wind, set every frame by the WindServer. Include it in a spatial shader:
//   #include "res://.builtin/wind.gdshaderinc"
// Grass (in vertex(), the blade's root at its origin, UV.y = 0 root to 1 tip):
//   vec3 root = MODEL_MATRIX[3].xyz;
//   float height = length(MODEL_MATRIX[1].xyz);
//   VERTEX += wind_to_local(MODEL_MATRIX, wind_grass_offset(root, UV.y, height, 6.0, TIME));
// Trees (in vertex(), branch and leaf weights from vertex colour):
//   vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
//   vec3 offset = wind_tree_offset(world, MODEL_MATRIX[3].xyz, 8.0, 12.0, COLOR.r, COLOR.g,
//           normalize(mat3(MODEL_MATRIX) * NORMAL), TIME);
//   VERTEX += wind_to_local(MODEL_MATRIX, offset);

// x, y: direction (world x, z), z: mean speed in m/s, w: gustiness 0..1.
global uniform vec4 wind_mean;
// x, y: gust pattern offset, z: 1 / gust size in m, w: gust direction swing in radians.
global uniform vec4 wind_gust;
// x: gust pattern drift, y: turbulence 0..1, z: number of wind zones.
global uniform vec4 wind_motion;
// Local wind zones (WindZone3D), 5 texels each: origin and flags; three rows
// of the world-to-unit basis with falloff, strength; the axis.
global uniform sampler2D wind_zones;

float wind_hash(vec2 p) {
	p = mod(p, 256.0);
	vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float wind_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = wind_hash(i);
	float b = wind_hash(i + vec2(1.0, 0.0));
	float c = wind_hash(i + vec2(0.0, 1.0));
	float d = wind_hash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Gust strength at a point, 0..1 (0.5 on average).
float wind_gust_at(vec3 world_position) {
	vec2 q = (world_position.xz - wind_gust.xy) * wind_gust.z;
	float g = wind_noise(q) * 0.65 + wind_noise(q * 2.0 + vec2(17.0, 5.0) + wind_motion.x) * 0.35;
	return clamp((g - 0.5) * 2.2 + 0.5, 0.0, 1.0);
}

// The local wind zones on top of the wind w at a point.
vec3 wind_zones_apply(vec3 world_position, vec3 w) {
	int count = int(wind_motion.z);
	for (int i = 0; i < count; i++) {
		int x = i * 5;
		vec4 t0 = texelFetch(wind_zones, ivec2(x, 0), 0);
		vec4 t1 = texelFetch(wind_zones, ivec2(x + 1, 0), 0);
		vec4 t2 = texelFetch(wind_zones, ivec2(x + 2, 0), 0);
		vec4 t3 = texelFetch(wind_zones, ivec2(x + 3, 0), 0);
		vec3 offset = world_position - t0.xyz;
		vec3 unit = vec3(dot(t1.xyz, offset), dot(t2.xyz, offset), dot(t3.xyz, offset));
		int flags = int(t0.w + 0.5);
		int mode = flags % 4;
		float edge = (flags / 4) % 2 == 0 ? length(unit) : max(max(abs(unit.x), abs(unit.y)), abs(unit.z));
		if (edge >= 1.0) {
			continue;
		}
		float weight = 1.0 - smoothstep(1.0 - t1.w, 1.0, edge);
		vec3 axis = texelFetch(wind_zones, ivec2(x + 4, 0), 0).xyz;
		vec3 zone = axis * t2.w;
		if (mode == 1) {
			float l = length(offset);
			zone = l > 0.001 ? offset * (t2.w / l) : vec3(0.0);
		} else if (mode == 2) {
			vec3 around = cross(axis, offset);
			float l = length(around);
			zone = l > 0.001 ? around * (t2.w / l) : vec3(0.0);
		}
		w = flags >= 8 ? mix(w, zone, weight) : w + zone * weight;
	}
	return w;
}

// Wind velocity at a point, in m/s: the world's wind (horizontal) plus
// the local zones (any direction).
vec3 wind_sample(vec3 world_position) {
	vec2 q = (world_position.xz - wind_gust.xy) * wind_gust.z;
	float g = wind_noise(q) * 0.65 + wind_noise(q * 2.0 + vec2(17.0, 5.0) + wind_motion.x) * 0.35;
	float gust = clamp((g - 0.5) * 2.2 + 0.5, 0.0, 1.0);
	float speed = wind_mean.z * (1.0 + wind_mean.w * (gust * 1.4 - 0.4));
	float swing = (wind_noise(q * 0.5 + vec2(41.0, 73.0)) - 0.5) * 2.0 * wind_gust.w * wind_mean.w;
	float c = cos(swing);
	float s = sin(swing);
	vec2 d = vec2(wind_mean.x * c - wind_mean.y * s, wind_mean.x * s + wind_mean.y * c);
	return wind_zones_apply(world_position, vec3(d.x, 0.0, d.y) * max(speed, 0.0));
}

// How far wind of this speed bends a plant, 0..1. stiffness: the speed in
// m/s that bends it about two thirds of the way.
float wind_bend_amount(float speed, float stiffness) {
	return 1.0 - exp(-speed / max(stiffness, 0.01));
}

// A world-space direction or offset into the model's local space.
vec3 wind_to_local(mat4 model, vec3 world_offset) {
	return inverse(mat3(model)) * world_offset;
}

// Grass: world-space offset of a point at height fraction h (0 root, 1 tip)
// on a blade `height` m tall rooted at `root`. The blade bends downwind,
// more toward the tip, and shivers in turbulent wind.
vec3 wind_grass_offset(vec3 root, float h, float height, float stiffness, float time) {
	// Plants bend with the horizontal wind (a downdraft doesn't bend them).
	vec3 w = wind_sample(root) * vec3(1.0, 0.0, 1.0);
	float speed = length(w);
	if (speed < 0.001) {
		return vec3(0.0);
	}
	vec3 dir = w / speed;
	float phase = dot(root.xz, vec2(1.7, 2.3));
	float flutter = sin(time * (4.0 + speed * 0.6) + phase) * wind_motion.y * min(speed * 0.08, 0.5);
	float angle = (wind_bend_amount(speed, stiffness) * 1.2 + flutter * 0.4) * h * h;
	float along = height * h * sin(angle);
	float down = height * h * (1.0 - cos(angle));
	return dir * along - vec3(0.0, down, 0.0);
}

// Trees and bushes: world-space offset of a vertex. root: the tree's base
// (MODEL_MATRIX[3].xyz), height: tree height in m, stiffness: speed in m/s
// that bends it two thirds of the way. branch: 0 trunk to 1 branch tips,
// leaf: 0 to 1 leaf flutter (vertex colour or a mask), normal: world normal.
vec3 wind_tree_offset(vec3 vertex, vec3 root, float height, float stiffness, float branch, float leaf, vec3 normal, float time) {
	// Plants bend with the horizontal wind (a downdraft doesn't bend them).
	vec3 w = wind_sample(root) * vec3(1.0, 0.0, 1.0);
	float speed = length(w);
	if (speed < 0.001) {
		return vec3(0.0);
	}
	vec3 dir = w / speed;
	float bend = wind_bend_amount(speed, stiffness);
	float h = clamp((vertex.y - root.y) / max(height, 0.01), 0.0, 1.0);
	float tree_phase = dot(root.xz, vec2(0.31, 0.17));
	// Trunk: leans downwind and rocks at its own frequency (tall trees slower).
	float frequency = 3.8 / sqrt(max(height, 1.0));
	float rock = sin(time * frequency + tree_phase) * 0.3 + sin(time * frequency * 2.3 + tree_phase * 1.7) * 0.1;
	vec3 offset = dir * (height * 0.06 * bend * (1.0 + rock * (0.4 + wind_mean.w)) * h * h);
	// Branches bob up and down, out of step with each other.
	float branch_phase = tree_phase + dot(vertex, vec3(0.7, 0.9, 0.8));
	float bob = sin(time * (frequency * 2.6) + branch_phase);
	offset += (vec3(dir.x * 0.4, 1.0, dir.z * 0.4) * bob) * (branch * 0.12 * bend);
	// Leaves flutter along their normal, faster in strong wind.
	float leaf_phase = dot(vertex, vec3(3.1, 2.7, 4.3));
	float flutter = sin(time * (9.0 + speed * 0.8) + leaf_phase);
	offset += normal * (flutter * leaf * 0.025 * (0.2 + wind_motion.y) * min(speed / 8.0, 1.5));
	return offset;
}
)";
