/**************************************************************************/
/*  weather_sky_shader.h                                                  */
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

// The built-in weather sky (WeatherSkyEffect::create_default_material()).
// The uniforms in the "Weather" group are set every frame by the effect.
static const char *weather_sky_shader_code = R"(// Weather sky: gradient atmosphere, sun disk and a 2D cloud layer.
//
// Performance: clouds are computed in the half-resolution sky pass (and
// directly in the low-res radiance cubemap pass); the full-resolution pass
// only draws the gradient and sun and composites the clouds. Clouds sample two
// small tiling noise textures instead of evaluating noise per pixel.
//
// The "Weather" uniforms are set by WeatherSkyEffect every frame.
shader_type sky;
render_mode use_half_res_pass;

group_uniforms Sky;
uniform vec3 sky_top_color : source_color = vec3(0.22, 0.38, 0.66);
uniform vec3 sky_horizon_color : source_color = vec3(0.62, 0.68, 0.76);
uniform vec3 ground_color : source_color = vec3(0.11, 0.11, 0.12);
uniform vec3 overcast_color : source_color = vec3(0.46, 0.48, 0.52);
// Higher values keep the horizon color lower in the sky.
uniform float horizon_falloff : hint_range(0.1, 2.0) = 0.5;
uniform float sky_energy : hint_range(0.0, 4.0) = 1.0;
// Visible sun disk size in degrees. Independent of the light's angular size,
// which the weather widens under clouds to soften shadows.
uniform float sun_disk_degrees : hint_range(0.0, 10.0) = 0.75;
uniform float sun_disk_energy : hint_range(0.0, 50.0) = 12.0;

group_uniforms Clouds;
uniform sampler2D cloud_shape : filter_linear_mipmap, repeat_enable;
uniform sampler2D cloud_detail : filter_linear_mipmap, repeat_enable;
// Height of the cloud layer in meters.
uniform float cloud_altitude = 1500.0;
// Shape texture repeats per meter of cloud layer.
uniform float cloud_scale = 0.00008;
uniform float detail_scale = 6.0;
// Edge softness. Lower = crisper, more defined clouds.
uniform float cloud_softness : hint_range(0.01, 1.0) = 0.2;
// How much the detail noise eats into cloud edges and lumps up the surface.
uniform float cloud_erosion : hint_range(0.0, 1.0) = 0.55;
// Brightness variation inside clouds (thick cores darker, thin parts brighter).
uniform float cloud_contrast : hint_range(0.0, 1.0) = 0.8;
uniform vec3 cloud_lit_color : source_color = vec3(1.0, 0.98, 0.95);
uniform vec3 cloud_shadow_color : source_color = vec3(0.56, 0.6, 0.68);
uniform vec3 storm_color : source_color = vec3(0.17, 0.18, 0.2);
uniform float cloud_light_energy : hint_range(0.0, 4.0) = 0.55;

group_uniforms Weather;
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.2;
uniform float cloud_density : hint_range(0.0, 1.0) = 0.7;
uniform float cloud_darkness : hint_range(0.0, 1.0) = 0.0;
uniform float overcast : hint_range(0.0, 1.0) = 0.0;
// Accumulated wind drift in meters.
uniform vec2 cloud_offset = vec2(0.0);
// Lightning: 0..1 flash strength and the direction of the strike in the sky.
uniform float lightning_flash = 0.0;
uniform vec3 lightning_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 lightning_color : source_color = vec3(0.75, 0.82, 1.0);

vec3 sky_gradient(vec3 dir) {
	vec3 color;
	if (dir.y >= 0.0) {
		color = mix(sky_horizon_color, sky_top_color, pow(dir.y, horizon_falloff));
	} else {
		color = mix(sky_horizon_color, ground_color, clamp(-dir.y * 6.0, 0.0, 1.0));
	}
	vec3 grey = overcast_color * (1.0 - 0.65 * cloud_darkness);
	return mix(color, grey, overcast);
}

float cloud_noise(vec2 uv, float detail) {
	float shape = texture(cloud_shape, uv).r;
	// Erode: detail cuts into the shape, mostly at the edges.
	return shape - (1.0 - detail) * cloud_erosion * 0.35 + detail * 0.15;
}

vec4 clouds(vec3 dir, vec3 sun_dir, vec3 sun_color) {
	if (dir.y <= 0.0 || cloud_coverage <= 0.0) {
		return vec4(0.0);
	}
	// Flattened dome: the layer stays finite toward the horizon.
	float distance_to_layer = cloud_altitude / (dir.y + 0.05);
	vec2 uv = (dir.xz * distance_to_layer + cloud_offset) * cloud_scale;
	// Detail drifts faster than the shape so clouds evolve as they move.
	float detail = texture(cloud_detail, uv * detail_scale + cloud_offset * cloud_scale).r;
	float noise = cloud_noise(uv, detail);

	float threshold = 1.0 - cloud_coverage;
	float density = smoothstep(threshold - cloud_softness * 0.5, threshold + cloud_softness * 0.5, noise) * cloud_density;

	// One extra sample toward the sun: more cloud there means this point is in shadow.
	vec2 to_sun = normalize(sun_dir.xz + vec2(1e-4)) * 0.015;
	float toward_sun = cloud_noise(uv + to_sun, detail);
	float light = clamp(0.75 - (toward_sun - noise) * 7.0 - density * 0.35, 0.0, 1.0);
	// Thickness shading: how far above the coverage threshold this point is.
	// Visible even under full cover, so a storm deck stays lumpy, not flat.
	float thickness = smoothstep(threshold, threshold + 0.45, noise);
	light *= mix(1.0, 1.0 - thickness, cloud_contrast);

	vec3 lit = cloud_lit_color * sun_color * cloud_light_energy;
	vec3 shadow = cloud_shadow_color * (0.35 + 0.65 * cloud_light_energy);
	// Under flat overcast light, detail and thickness still shade the cloud base.
	vec3 color = mix(shadow * mix(1.0, (0.45 + 0.55 * detail) * (1.25 - 0.5 * thickness), cloud_contrast), lit, light);
	// Thin cloud edges glow when looking toward the sun.
	float forward_scatter = pow(max(dot(dir, sun_dir), 0.0), 6.0) * (1.0 - density) * 1.5;
	color += lit * forward_scatter;
	// Storm clouds: dark, but keep the thickness variation so the deck has texture.
	vec3 storm = storm_color * mix(1.7, 0.5, thickness) * mix(0.7, 1.3, detail);
	color = mix(color, storm, cloud_darkness * smoothstep(0.2, 1.0, density));
	// Lightning lights the clouds from inside, strongest around the strike.
	if (lightning_flash > 0.0) {
		float near_strike = pow(max(dot(dir, lightning_direction), 0.0), 6.0);
		color += lightning_color * lightning_flash * density * (0.4 + 3.0 * near_strike);
	}

	float horizon_fade = smoothstep(0.0, 0.06, dir.y);
	return vec4(color, density * horizon_fade);
}

void sky() {
	vec3 sun_dir = LIGHT0_ENABLED ? LIGHT0_DIRECTION : vec3(0.0, 1.0, 0.0);
	vec3 sun_color = LIGHT0_ENABLED ? LIGHT0_COLOR * LIGHT0_ENERGY : vec3(1.0);

	if (AT_HALF_RES_PASS) {
		vec4 c = clouds(EYEDIR, sun_dir, sun_color);
		COLOR = c.rgb;
		ALPHA = c.a;
	} else {
		vec3 color = sky_gradient(EYEDIR);
		if (LIGHT0_ENABLED) {
			float sun_angle = acos(clamp(dot(EYEDIR, sun_dir), -1.0, 1.0));
			float disk_size = max(radians(sun_disk_degrees), 0.005);
			float disk = 1.0 - smoothstep(disk_size * 0.7, disk_size, sun_angle);
			float halo = pow(max(dot(EYEDIR, sun_dir), 0.0), 64.0) * 0.25;
			// The sun disk disappears well before the sky is fully closed.
			float clear_sky = 1.0 - smoothstep(0.1, 0.5, overcast);
			color += sun_color * (disk * sun_disk_energy + halo) * clear_sky;
		}
		color += lightning_color * lightning_flash * overcast * 0.25;
		vec4 c = AT_CUBEMAP_PASS ? clouds(EYEDIR, sun_dir, sun_color) : HALF_RES_COLOR;
		COLOR = mix(color, c.rgb, c.a) * sky_energy;
	}
}
)";
