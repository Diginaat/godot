/**************************************************************************/
/*  weather_haze_shaders.h                                                */
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

// Shaders of WeatherHazeEffect and WeatherMotesEffect, kept as code so the
// module needs no files.

// Ground mist, rain shafts and spray, for a FogVolume.
static const char *weather_haze_shader_code = R"(// Weather haze for WeatherHazeEffect's FogVolume. Three layers, all lit by the
// volumetric fog like real haze (no geometry, so no planes or hard edges):
//  - mist: densest at the ground, thinning out with height, broken up into
//    large patches that drift with the wind;
//  - rain: tall, soft shafts of heavier and lighter rain that lean with the
//    wind, drift with it and slowly change shape; they reach up to the clouds;
//  - spray: a thin layer of splash spray hugging the ground in heavy rain.
// Everything is computed in world space (ground level, noise, drift), so the
// volume can follow the camera without the haze moving with it.
shader_type fog;

// Lit by the scene's lights and by the sky (the environment's ambient
// inject, which WeatherHazeEffect sets from its brightness). No
// EMISSION: Godot packs fog volume emission times density into very few bits
// (511 steps red/green, 255 blue, truncated), so dim emission comes out as
// a green tint.
uniform vec3 albedo : source_color = vec3(1.0);
// World height of the ground under the camera (smoothed by the effect).
uniform float ground_height = 0.0;

// Mist density at the ground.
uniform float mist_density = 0.0;
// Mist density halves every 1 / height_falloff meters above the ground.
uniform float height_falloff = 0.15;
// 0 = even mist, 1 = clear gaps between thick patches.
uniform float patchiness : hint_range(0.0, 1.0) = 0.6;

// Average rain haze density.
uniform float rain_density = 0.0;
// Rain haze density halves every rain_height meters.
uniform float rain_height = 15.0;
// Horizontal wind divided by the fall speed: how far the shafts lean per meter of height.
uniform vec2 rain_slant = vec2(0.0);

// Spray density at the ground.
uniform float spray_density = 0.0;

// Wisps: fine detail that breaks the haze up close (0 = smooth haze).
uniform float wisp_strength : hint_range(0.0, 1.0) = 0.45;

// Tileable 3D noise.
uniform sampler3D noise_texture : hint_default_white, filter_linear, repeat_enable;
// Size of one noise tile in meters.
uniform float patch_size = 96.0;
// xz: wind drift in meters, wrapped every patch_size. y: shaft evolution, in tiles.
uniform vec3 drift = vec3(0.0);
// Width of the soft fade at the volume's sides and top, as a fraction of its size.
uniform float edge_fade = 0.08;

void fog() {
	float height = WORLD_POSITION.y - ground_height;
	vec3 p = (WORLD_POSITION - vec3(drift.x, 0.0, drift.z)) / patch_size;

	// Mist patches: flattened (mist is wider than it is tall), plus a finer
	// layer. Integer tile multiples, so wrapping the drift stays seamless.
	float mist_noise = texture(noise_texture, p * vec3(1.0, 2.5, 1.0)).r * 0.65
			+ texture(noise_texture, p * 3.0 + vec3(0.37)).r * 0.35;
	float patches = mix(1.0, smoothstep(0.25, 0.75, mist_noise) * 2.0, patchiness);
	float d = mist_density * clamp(exp2(-height_falloff * height), 0.0, 1.0) * patches;

	// Rain shafts: vertical columns (the noise's y axis is time, not height),
	// leaning downwind: rain at this height lands rain_slant * height downwind.
	vec2 shaft_uv = (WORLD_POSITION.xz - drift.xz + rain_slant * max(height, 0.0)) / patch_size;
	float shaft = texture(noise_texture, vec3(shaft_uv.x + 0.61, drift.y + height / (patch_size * 4.0), shaft_uv.y + 0.23)).r;
	d += rain_density * mix(0.3, 1.7, smoothstep(0.3, 0.75, shaft)) * exp2(-max(height, 0.0) / rain_height);

	// Spray: full up to 0.6 m, halving every 0.4 m above.
	d += spray_density * clamp(exp2(-2.5 * (height - 0.6)), 0.0, 1.0) * mix(0.6, 1.4, mist_noise);

	// Wisps: a finer layer (about 5 m features), so the haze near the camera
	// is broken up instead of one smooth wash. Integer tile multiple again.
	float wisps = texture(noise_texture, p * 9.0 + vec3(0.71, 0.13, 0.47)).r;
	d *= mix(1.0, smoothstep(0.2, 0.8, wisps) * 2.0, wisp_strength);

	// Soft sides and top, so the box's position steps never show.
	float side = min(min(UVW.x, 1.0 - UVW.x), min(UVW.z, 1.0 - UVW.z));
	d *= smoothstep(0.0, edge_fade, side) * smoothstep(0.0, edge_fade, 1.0 - UVW.y);

	DENSITY = d;
	ALBEDO = albedo;
}
)";

// A mist droplet, for the motes' GPUParticles3D draw pass.
static const char *weather_mote_shader_code = R"(// Mist mote for WeatherMotesEffect's GPUParticles3D draw pass (QuadMesh 1x1): a tiny
// soft droplet floating in the air. Thousands of them around the camera make
// fog and rain read as countless small particles instead of a smooth wash,
// which the volumetric fog can't do (its froxels are far too coarse).
//
// Lit by the scene's lights, with strong light from behind (droplets scatter
// forward), so they glitter in light beams like real mist. The overcast sky's
// light (set by the effect) keeps them faintly visible under clouds.
// Never drawn smaller than min_pixel_size; when widened, they fade to match,
// so distant motes don't shimmer.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, shadows_disabled, skip_vertex_transform, diffuse_lambert_wrap, specular_disabled;

uniform vec3 mote_color : source_color = vec3(0.86, 0.89, 0.93);
// Droplet size in meters.
uniform float size = 0.006;
uniform float min_pixel_size = 2.5;
uniform float opacity : hint_range(0.0, 1.0) = 0.65;
// Linear light of the sky near the horizon.
uniform vec3 sky_light = vec3(0.0);
uniform float sky_scatter : hint_range(0.0, 8.0) = 2.0;
// Light passing through from behind (looking toward a light makes them glitter).
uniform float translucency : hint_range(0.0, 2.0) = 1.0;
uniform float near_fade = 0.4;
uniform float far_fade = 14.0;
// Wind drift in meters (x, z), accumulated by the effect and wrapped by box_size.
uniform vec2 wind_offset = vec2(0.0);
// Width of the box around the camera the motes wrap in (x, z). Keep far_fade
// below half of it, so motes are invisible where they wrap.
uniform vec2 box_size = vec2(28.0);

varying float fade;

float hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void vertex() {
	vec3 center = MODEL_MATRIX[3].xyz;
	// All motes drift with the wind and wrap around the camera, so they follow
	// any wind direction and speed at once and always fill the box.
	vec2 around = center.xz + wind_offset - CAMERA_POSITION_WORLD.xz + box_size * 0.5;
	center.xz = CAMERA_POSITION_WORLD.xz + mod(around, box_size) - box_size * 0.5;
	vec3 to_camera = CAMERA_POSITION_WORLD - center;
	float distance_to_camera = length(to_camera);
	// Size of one pixel in meters at this distance. abs(): the projection's
	// y axis can be flipped (Vulkan), which would make this negative.
	float pixel_size = 2.0 * distance_to_camera / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	float seed = hash(float(INSTANCE_ID));
	float mote_size = size * mix(0.5, 1.5, seed);
	float drawn = max(mote_size, pixel_size * min_pixel_size);
	vec3 right = INV_VIEW_MATRIX[0].xyz;
	vec3 up = INV_VIEW_MATRIX[1].xyz;
	vec3 world = center + (right * VERTEX.x + up * VERTEX.y) * drawn;
	VERTEX = (VIEW_MATRIX * vec4(world, 1.0)).xyz;
	NORMAL = (VIEW_MATRIX * vec4(to_camera / max(distance_to_camera, 1e-4), 0.0)).xyz;

	// Fade in and out over the mote's life, near the lens and at the edge of
	// the volume; widened motes fade (gently, so far ones still read).
	float life = INSTANCE_CUSTOM.y;
	fade = smoothstep(0.0, 0.2, life) * (1.0 - smoothstep(0.7, 1.0, life))
			* smoothstep(near_fade * 0.5, near_fade, distance_to_camera)
			* (1.0 - smoothstep(far_fade * 0.6, far_fade, distance_to_camera))
			* pow(mote_size / drawn, 0.35)
			* mix(0.4, 1.0, hash(seed * 5.3));
}

void fragment() {
	float r = length(UV * 2.0 - 1.0);
	ALBEDO = mote_color;
	BACKLIGHT = mote_color * translucency;
	EMISSION = sky_light * sky_scatter;
	ALPHA = opacity * (1.0 - smoothstep(0.15, 1.0, r)) * fade;
}
)";
