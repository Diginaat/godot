/**************************************************************************/
/*  weather_lightning_shader.h                                            */
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

// The lightning bolt shader of WeatherLightningEffect, kept as code so the
// module needs no files.

static const char *weather_lightning_shader_code = R"(// Lightning bolt channel for WeatherLightningEffect: camera-facing ribbons
// along the bolt's polylines, drawn additively as a white-hot core inside a
// wider blue-violet glow (the environment's glow adds bloom on top).
//
// Mesh layout: VERTEX = point on the channel, NORMAL = channel direction
// there, UV.x = side (-1/+1), UV.y = how far down the main channel this point
// is (0 at the cloud, 1 at the ground), UV2.x = width scale, UV2.y =
// brightness (1 = main channel, less for branches).
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled, skip_vertex_transform;

uniform vec3 core_color : source_color = vec3(1.0, 0.98, 1.0);
uniform vec3 glow_color : source_color = vec3(0.55, 0.62, 1.0);
uniform float core_energy = 30.0;
uniform float glow_energy = 3.0;
// Core width in meters; it never gets thinner than min_core_pixels on screen.
uniform float core_width = 1.5;
uniform float min_core_pixels = 1.5;
// Glow width in meters (each side of the channel).
uniform float glow_width = 12.0;

// Set every frame by the effect.
uniform float flash = 0.0;
// Brightness of the branches (they show mostly in the leader and first stroke).
uniform float branch_flash = 1.0;
// How far down the leader has reached (0..1): the channel grows from the cloud.
uniform float reveal = 1.0;
// Dimming by rain and haze between the camera and the strike.
uniform float visibility = 1.0;

varying float across;
varying float core_ratio;
varying float progress;
varying float brightness;

void vertex() {
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 direction = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 to_camera = CAMERA_POSITION_WORLD - world;
	float distance_to_camera = length(to_camera);
	vec3 side = normalize(cross(direction, to_camera / max(distance_to_camera, 1e-3)));
	// Size of one pixel in meters there. abs(): the projection's y axis can be
	// flipped (Vulkan).
	float pixel_size = 2.0 * distance_to_camera / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	float core = max(core_width * UV2.x, pixel_size * min_core_pixels);
	float half_width = max(glow_width * UV2.x, core * 6.0);
	world += side * UV.x * half_width;
	VERTEX = (VIEW_MATRIX * vec4(world, 1.0)).xyz;
	across = UV.x;
	core_ratio = core / half_width;
	progress = UV.y;
	brightness = UV2.y;
}

void fragment() {
	if (progress > reveal) {
		discard;
	}
	float x = across;
	float core = exp(-x * x / max(core_ratio * core_ratio, 1e-6) * 2.0);
	float glow = exp(-x * x * 5.0);
	float is_branch = step(brightness, 0.99);
	float intensity = flash * brightness * mix(1.0, branch_flash, is_branch) * visibility;
	// The channel comes out of the cloud: fade it in over the top part.
	intensity *= smoothstep(0.0, 0.15, progress);
	// The tip of a descending leader glows brightest.
	float tip = reveal < 1.0 ? 1.0 + 2.0 * smoothstep(reveal - 0.05, reveal, progress) : 1.0;
	ALBEDO = (core_color * core * core_energy + glow_color * glow * glow_energy) * intensity * tip;
}
)";
