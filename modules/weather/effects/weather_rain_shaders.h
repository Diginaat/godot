/**************************************************************************/
/*  weather_rain_shaders.h                                                */
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

// Shaders of WeatherRainEffect, kept as code so the module needs no files.

// Raindrops: a streak per particle.
static const char *weather_rain_streak_shader_code = R"(// Rain streak for GPUParticles3D draw passes (QuadMesh 1x1).
// Each quad is stretched along the particle's Y axis (aligned to velocity by
// the process material) and turned toward the camera around that axis.
//
// The streak is lit like a thin water cylinder: a curved normal across its
// width picks up sun highlights, sky reflections and lightning flashes, and
// it refracts the scene behind it like a real drop (screen texture sampled
// with an offset). Shadows are skipped to stay cheap.
// Streaks are never drawn thinner than min_pixel_width; when widened, their
// opacity drops to match, so distant rain stays visible and doesn't shimmer.
//
// Each streak gets its own procedural "texture", after Garg & Nayar's
// measurements of real rain ("Photorealistic rendering of rain streaks",
// SIGGRAPH 2006): falling drops wobble (oscillate), so one motion-blurred
// streak shows brighter and dimmer beads along its length, bends slightly
// and changes width. Near drops are out of focus: wider, softer and fainter,
// as a camera sees them, instead of sharp scratches across the screen.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, shadows_disabled, skip_vertex_transform, diffuse_lambert, specular_schlick_ggx;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear_mipmap;
uniform vec3 rain_color : source_color = vec3(0.6, 0.66, 0.72);
// How strongly drops bend the background (0 = no refraction).
uniform float refraction : hint_range(0.0, 0.05) = 0.016;
uniform float opacity : hint_range(0.0, 1.0) = 0.26;
// Rougher than still water: a motion-blurred, wobbling drop spreads the
// highlight over a wide angle, so lights catch the rain softly, not as pinpricks.
uniform float roughness : hint_range(0.0, 1.0) = 0.45;
// Small self-lit term for light scattered by the water, so rain reads in dark storms.
uniform float glint : hint_range(0.0, 1.0) = 0.08;
// Self-lit glow in the rain colour (set by WeatherRainEffect). Not physical:
// it keeps rain visible at night and against backgrounds the refraction
// would otherwise blend it into.
uniform float emission : hint_range(0.0, 4.0) = 0.5;
// Light passing through the drop from behind.
uniform float translucency : hint_range(0.0, 1.0) = 0.40;
// Linear light of the sky near the horizon (set by WeatherRainEffect). A drop is a
// tiny fisheye lens: it refracts mostly the bright sky around it, not the
// ground straight behind, and reflects the sky at its edges. That's what
// makes real rain read as light streaks against dark ground under clouds.
uniform vec3 sky_light = vec3(0.0);
uniform float sky_reflection : hint_range(0.0, 4.0) = 1.6;
// How much of the refracted image is sky rather than the scene behind.
uniform float sky_refraction : hint_range(0.0, 1.0) = 0.6;
uniform float streak_length = 0.55;
uniform float streak_width = 0.008;
uniform float min_pixel_width = 1.5;
// Drops closer than this fade out, like out-of-focus drops near a camera lens
// (closer ones would draw as long scratches across the screen).
uniform float near_fade = 2.5;
uniform float far_fade = 32.0;
// Strength of the oscillation beads along each streak (0 = plain streak).
uniform float oscillation : hint_range(0.0, 1.0) = 0.45;
// How far the wobble bends a streak sideways, in streak widths.
uniform float wobble : hint_range(0.0, 0.5) = 0.18;
// Drops closer than this are drawn out of focus (wider, softer, fainter).
uniform float focus_distance = 6.0;

varying float fade;
varying float drop_seed;
varying float defocus;

float hash(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void vertex() {
	vec3 center = MODEL_MATRIX[3].xyz;
	vec3 axis_y = normalize(MODEL_MATRIX[1].xyz);
	vec3 to_camera = CAMERA_POSITION_WORLD - center;
	float distance_to_camera = length(to_camera);
	vec3 view_dir = to_camera / max(distance_to_camera, 1e-4);
	vec3 axis_x = normalize(cross(axis_y, view_dir));
	vec3 facing = normalize(cross(axis_x, axis_y));

	// Size of one pixel in meters at this distance.
	// abs(): the projection's y axis can be flipped (Vulkan), which would make
	// this negative and silently skip the minimum width.
	float pixel_size = 2.0 * distance_to_camera / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	float width = max(streak_width, pixel_size * min_pixel_width);
	// Real drops vary in size: per-drop length, width and brightness.
	float seed = hash(float(INSTANCE_ID));
	drop_seed = seed;
	float drop_scale = length(MODEL_MATRIX[1].xyz);
	width *= mix(0.7, 1.4, seed);
	// Out of focus near the camera: spread over a wider, fainter streak.
	defocus = 1.0 - smoothstep(near_fade, focus_distance, distance_to_camera);
	float blur = 1.0 + 1.5 * defocus;
	width *= blur;
	float length_m = streak_length * drop_scale * mix(0.6, 1.3, hash(seed * 7.1));
	vec3 world = center + axis_x * VERTEX.x * width + axis_y * VERTEX.y * length_m;
	VERTEX = (VIEW_MATRIX * vec4(world, 1.0)).xyz;
	NORMAL = (VIEW_MATRIX * vec4(facing, 0.0)).xyz;
	TANGENT = (VIEW_MATRIX * vec4(axis_x, 0.0)).xyz;
	BINORMAL = (VIEW_MATRIX * vec4(axis_y, 0.0)).xyz;

	fade = smoothstep(near_fade * 0.5, near_fade, distance_to_camera)
			* (1.0 - smoothstep(far_fade * 0.6, far_fade, distance_to_camera))
			* pow(streak_width * blur / width, 0.3) / blur
			* mix(0.45, 1.0, hash(seed * 3.3));
}

void fragment() {
	// Oscillation: each drop wobbles with its own rate and phase, which the
	// exposure smears into beads along the streak; out of focus they blur away.
	float cycles = mix(2.0, 7.0, hash(drop_seed * 13.7));
	float phase = hash(drop_seed * 29.3) * TAU;
	float wave = sin(UV.y * cycles * TAU + phase);
	float beads = 1.0 + oscillation * (1.0 - defocus) * wave;
	// The wobble bends the streak a little and makes it fatter where brighter.
	float bend_x = wobble * sin(UV.y * cycles * 0.5 * TAU + phase * 1.7);
	float half_width = 0.75 + 0.15 * wave * (1.0 - defocus);
	float x = clamp((UV.x * 2.0 - 1.0 - bend_x) / half_width, -1.0, 1.0);
	// Curved normal across the width, like a cylinder of water.
	NORMAL = normalize(NORMAL * sqrt(max(1.0 - x * x, 0.0)) + TANGENT * x);
	// Refraction: the drop shows the scene behind it, shifted by its curved
	// surface and slightly brightened (a drop focuses the light behind it).
	vec2 bend = NORMAL.xy * refraction;
	vec3 behind = textureLod(screen_texture, SCREEN_UV - bend, 0.0).rgb;
	ALBEDO = rain_color * 0.15;
	ROUGHNESS = roughness;
	SPECULAR = 0.8;
	BACKLIGHT = rain_color * translucency;
	float fresnel = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 3.0);
	vec3 refracted = mix(behind, sky_light * sky_reflection, sky_refraction);
	EMISSION = refracted + rain_color * (glint + emission) + sky_light * sky_reflection * fresnel * 0.5;
	// Motion-blurred drop: brightest at the leading end, fading along the tail.
	// The quad's +Y points along the velocity, so the leading end is UV.y = 0.
	float tail = 1.0 - UV.y;
	float along = smoothstep(0.0, 0.85, tail) * (1.0 - smoothstep(0.92, 1.0, tail));
	// Soft cross profile (sharper in focus, softer out of focus).
	float profile = exp(-x * x * mix(3.5, 1.8, defocus)) * (1.0 - x * x);
	ALPHA = opacity * profile * along * beads * fade;
}
)";

// Impacts: ring, droplet crown, crown sheet and mist, picked by the mode uniform.
static const char *weather_rain_impact_shader_code = R"(// Rain impact, drawn for each impact particle (one per raindrop hitting a
// surface). The particle never moves; all motion is computed here from its
// lifetime, so an impact costs one particle.
//
// mode 0 (ring):  a thin ring that expands on the surface. Only on wet
//                 surfaces, like a real puddle; dry ground just gets darker.
// mode 1 (crown): droplets thrown up and pulled back by gravity, each drawn
//                 stretched along its velocity (motion blur). The mesh has one
//                 quad per droplet; COLOR.r holds the droplet index (0..1).
//                 Small drops make no crown; crown size varies per impact.
// mode 2 (mist):  a faint puff of spray that rises and spreads, so heavy rain
//                 builds a low ground mist.
// mode 3 (sheet): the crown itself: a thin translucent cup of water that rises
//                 from the impact, flares out with a jagged rim and collapses,
//                 refracting the scene behind it. The mesh is an open cylinder;
//                 UV.x is the angle around it (0..1), UV.y the height (0..1).
// Ring and crown are lit, so they pick up sky reflections and lightning.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, shadows_disabled, skip_vertex_transform, diffuse_lambert, specular_schlick_ggx;

global uniform float weather_wetness;

uniform sampler2D screen_texture : hint_screen_texture, filter_linear_mipmap;

uniform int mode = 0;
uniform vec3 water_color : source_color = vec3(0.62, 0.68, 0.75);
uniform float opacity : hint_range(0.0, 1.0) = 0.02;
// Small self-lit term for light scattered by the water, so impacts read in dark storms.
uniform float glint : hint_range(0.0, 1.0) = 0.15;
// Splashes are churned water: a soft, wide highlight rather than a sharp glint.
uniform float roughness : hint_range(0.0, 1.0) = 0.3;
// Linear light of the sky near the horizon (set by WeatherRainEffect), reflected by
// rings and splashes so they read under clouds.
uniform vec3 sky_light = vec3(0.0);
uniform float sky_reflection : hint_range(0.0, 2.0) = 0.6;
// Particle lifetime in seconds; must match the emitter.
uniform float lifetime = 0.42;
uniform float ring_max_radius = 0.05;
uniform float droplet_size = 0.004;
uniform float droplet_speed = 1.8;
uniform float droplet_rise = 1.3;
// Fraction of impacts that throw up a crown (the rest are small drops: ring only).
uniform float crown_chance : hint_range(0.0, 1.0) = 0.4;
// Simulated camera shutter for the droplet motion blur, in seconds.
uniform float motion_blur = 0.02;
uniform float crown_radius = 0.04;
uniform float crown_height = 0.06;
uniform float crown_refraction = 0.2;
uniform float mist_size = 0.20;
uniform float mist_opacity : hint_range(0.0, 1.0) = 0.05;

varying float life;
varying float fade;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void vertex() {
	// ParticleProcessMaterial stores lifetime progress (0..1) in INSTANCE_CUSTOM.y.
	life = INSTANCE_CUSTOM.y;
	vec3 origin = MODEL_MATRIX[3].xyz;
	float seed = hash(origin.xz);
	vec3 world = origin;
	vec3 normal = vec3(0.0, 1.0, 0.0);
	fade = 1.0;

	if (mode == 0) {
		float grow = 1.0 - (1.0 - life) * (1.0 - life);
		float radius = ring_max_radius * (0.5 + 0.5 * seed) * grow;
		world = origin + vec3(VERTEX.x * radius * 2.0, 0.003, VERTEX.z * radius * 2.0);
		fade = clamp(weather_wetness * 1.5 - 0.2, 0.0, 1.0);
	} else if (mode == 1) {
		float index = COLOR.r;
		// Small drops don't splash a crown, and crowns vary in droplet count.
		bool has_crown = seed < crown_chance;
		bool has_droplet = hash(vec2(index, seed)) < 0.4 + 0.6 * fract(seed * 7.3);
		// Droplets are thrown up from the crown's rim, mostly upward.
		float angle = (index + seed) * TAU;
		float speed = droplet_speed * (0.25 + 0.45 * hash(vec2(index, seed + 1.0)));
		float rise = droplet_rise * (0.7 + 0.5 * hash(vec2(seed, index)));
		float t = life * lifetime;
		vec3 velocity = vec3(cos(angle) * speed, rise - 9.8 * t, sin(angle) * speed);
		vec3 rim = vec3(cos(angle), 0.0, sin(angle)) * crown_radius;
		vec3 offset = rim + vec3(cos(angle) * speed * t, crown_height * 0.6 + rise * t - 4.9 * t * t, sin(angle) * speed * t);
		fade = (has_crown && has_droplet && offset.y > -0.005) ? 1.0 : 0.0;
		offset.y = max(offset.y, 0.0);

		// Quad stretched along the velocity and turned to face the camera.
		vec3 center = origin + offset;
		vec3 along = normalize(velocity + vec3(0.0, 1e-4, 0.0));
		vec3 to_camera = normalize(CAMERA_POSITION_WORLD - center);
		vec3 across = normalize(cross(along, to_camera));
		float width = droplet_size * (0.6 + 0.6 * hash(vec2(seed, index + 2.0)));
		float stretch = width + length(velocity) * motion_blur;
		world = center + across * VERTEX.x * width + along * VERTEX.y * stretch;
		normal = to_camera;
	} else if (mode == 3) {
		// Crown sheet: an open cup that grows, flares and collapses.
		float size = 0.7 + 0.6 * hash(vec2(seed, 9.1));
		float grow = 1.0 - (1.0 - life) * (1.0 - life);
		float angle = UV.x * TAU;
		// Jagged rim: each column of the mesh gets its own height.
		float spike = 0.55 + 0.45 * hash(vec2(floor(UV.x * 64.0 + 0.5), seed));
		float height = crown_height * size * sin(PI * min(life * 1.3, 1.0)) * mix(1.0, spike, UV.y);
		float radius = crown_radius * size * (0.35 + 0.65 * grow) * mix(1.0, 1.6, UV.y);
		vec3 out_dir = vec3(cos(angle), 0.0, sin(angle));
		world = origin + out_dir * radius + vec3(0.0, UV.y * height, 0.0);
		normal = normalize(out_dir + vec3(0.0, 0.4, 0.0));
		fade = (seed < crown_chance) ? clamp(weather_wetness * 2.0, 0.0, 1.0) : 0.0;
	} else {
		// Mist puff: camera-facing, rises a little and spreads.
		float size = mist_size * (0.3 + 0.7 * life) * (0.6 + 0.6 * seed);
		vec3 right = INV_VIEW_MATRIX[0].xyz;
		vec3 up = INV_VIEW_MATRIX[1].xyz;
		world = origin + vec3(0.0, 0.05 + 0.15 * life, 0.0) + (right * VERTEX.x + up * VERTEX.y) * size;
		normal = INV_VIEW_MATRIX[2].xyz;
	}

	VERTEX = (VIEW_MATRIX * vec4(world, 1.0)).xyz;
	NORMAL = (VIEW_MATRIX * vec4(normal, 0.0)).xyz;
}

void fragment() {
	ALBEDO = water_color;
	vec3 sky = sky_light * sky_reflection;
	EMISSION = water_color * glint + sky;
	ROUGHNESS = roughness;
	SPECULAR = 0.5;
	float r = length(UV * 2.0 - 1.0);
	if (mode == 0) {
		float ring = smoothstep(0.8, 0.92, r) * (1.0 - smoothstep(0.92, 1.0, r));
		ALPHA = opacity * ring * (1.0 - life) * fade;
	} else if (mode == 1) {
		ALPHA = opacity * (1.0 - smoothstep(0.35, 1.0, r)) * (1.0 - life * life) * fade;
	} else if (mode == 3) {
		// Thin water: refracts the scene behind it, brightest along the rim.
		vec3 behind = textureLod(screen_texture, SCREEN_UV - NORMAL.xy * crown_refraction, 0.0).rgb;
		EMISSION = behind * 1.1 + water_color * glint + sky;
		ALBEDO = water_color * 0.2;
		float rim = smoothstep(0.6, 1.0, UV.y);
		ALPHA = opacity * mix(0.25, 0.9, rim) * (1.0 - life * life) * fade;
	} else {
		EMISSION = water_color * glint;
		ALPHA = mist_opacity * (1.0 - smoothstep(0.0, 1.0, r)) * sin(life * PI);
	}
}
)";

// Turns the top-down occlusion camera's depth into world height.
static const char *weather_rain_capture_shader_code = R"(// Full-screen pass for the rain occlusion camera (WeatherRainEffect). Converts the
// top-down depth buffer into world-space height (meters), written to a float
// viewport texture: the height of the highest surface above each point.
//
// The quad only draws for orthographic cameras, so the player's perspective
// camera never sees it.
shader_type spatial;
render_mode unshaded, depth_test_disabled, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled, skip_vertex_transform;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

void vertex() {
	// Orthographic projections have [3][3] == 1; perspective ones have 0.
	if (PROJECTION_MATRIX[3][3] < 0.5) {
		POSITION = vec4(2.0, 2.0, 2.0, 1.0);
	} else {
		POSITION = vec4(VERTEX.xy * 2.0, 1.0, 1.0);
	}
}

void fragment() {
	float depth = texture(depth_texture, SCREEN_UV).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, depth, 1.0);
	view.xyz /= view.w;
	vec3 world = (INV_VIEW_MATRIX * vec4(view.xyz, 1.0)).xyz;
	ALBEDO = vec3(world.y);
}
)";
