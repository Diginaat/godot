/**************************************************************************/
/*  weather_lighting_effect.cpp                                           */
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

#include "weather_lighting_effect.h"

#include "../weather.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/3d/light_3d.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/environment.h"

void WeatherLightingEffect::update(Weather *p_weather, double p_delta) {
	_apply_sun(p_weather);
	_apply_exposure(p_weather);
	_apply_environment(p_weather);
	WeatherEffect::update(p_weather, p_delta);
}

void WeatherLightingEffect::_apply_sun(Weather *p_weather) {
	DirectionalLight3D *sun = p_weather->get_sun();
	if (!sun) {
		return;
	}
	const Ref<WeatherProfile> current = p_weather->get_current();
	const float overcast = current->get_param(WeatherProfile::PARAM_OVERCAST);
	const float sun_through_clouds = p_weather->get_sun_through_clouds();
	// A thick overcast leaves no direct sunlight; the sky lights everything.
	// Smooth curve: gone well before the sky is fully closed.
	const float direct_sun = current->get_param(WeatherProfile::PARAM_SUN_ENERGY_SCALE) * (1.0f - Math::smoothstep(0.25f, 0.8f, overcast));
	p_weather->drive(sun, SNAME("light_energy"), (float)p_weather->get_base(sun, SNAME("light_energy")) * direct_sun);
	// Fog sits under the clouds, so it only scatters sunlight that gets through.
	p_weather->drive(sun, SNAME("light_volumetric_fog_energy"), (float)p_weather->get_base(sun, SNAME("light_volumetric_fog_energy")) * sun_through_clouds);
	// Clouds spread the sunlight like a softbox: softer shadows as the sky closes.
	p_weather->drive(sun, SNAME("shadow_blur"), Math::lerp((float)p_weather->get_base(sun, SNAME("shadow_blur")), overcast_shadow_blur, 1.0f - sun_through_clouds));
	// Diffuse light under clouds makes no sharp highlights (wet floors would
	// otherwise show a hard sun glint even in a storm).
	p_weather->drive(sun, SNAME("light_specular"), (float)p_weather->get_base(sun, SNAME("light_specular")) * (1.0f - Math::smoothstep(0.1f, 0.5f, overcast)));
	// A sun with no energy left is switched off entirely: one less light.
	p_weather->drive(sun, SNAME("visible"), (bool)p_weather->get_base(sun, SNAME("visible")) && direct_sun > 0.0005f);
	// Shadows fade out with the sun instead of switching off (no popping).
	const float shadow_fade = Math::smoothstep(0.0f, 0.2f, direct_sun);
	p_weather->drive(sun, SNAME("shadow_opacity"), (float)p_weather->get_base(sun, SNAME("shadow_opacity")) * shadow_fade);
	p_weather->drive(sun, SNAME("shadow_enabled"), (bool)p_weather->get_base(sun, SNAME("shadow_enabled")) && shadow_fade > 0.001f);
}

void WeatherLightingEffect::_apply_exposure(Weather *p_weather) {
	// Darkens the picture for thick clouds. Auto exposure would undo a plain
	// exposure change, so with it on, dark weather lowers its target brightness.
	Ref<CameraAttributes> attributes = p_weather->get_camera_attributes();
	if (attributes.is_null()) {
		return;
	}
	const float brightness = p_weather->get_current()->get_param(WeatherProfile::PARAM_SCENE_BRIGHTNESS);
	const float auto_scale = p_weather->get_base(attributes.ptr(), SNAME("auto_exposure_scale"));
	const float multiplier = p_weather->get_base(attributes.ptr(), SNAME("exposure_multiplier"));
	if (attributes->is_auto_exposure_enabled()) {
		p_weather->drive(attributes.ptr(), SNAME("auto_exposure_scale"), auto_scale * brightness);
		p_weather->drive(attributes.ptr(), SNAME("exposure_multiplier"), multiplier);
	} else {
		p_weather->drive(attributes.ptr(), SNAME("auto_exposure_scale"), auto_scale);
		p_weather->drive(attributes.ptr(), SNAME("exposure_multiplier"), multiplier * brightness);
	}
}

void WeatherLightingEffect::_apply_environment(Weather *p_weather) {
	Ref<Environment> environment = p_weather->get_environment();
	if (environment.is_null()) {
		return;
	}
	Environment *env = environment.ptr();
	const Ref<WeatherProfile> current = p_weather->get_current();
	const float cloud_darkness = current->get_param(WeatherProfile::PARAM_CLOUD_DARKNESS);
	const float overcast = current->get_param(WeatherProfile::PARAM_OVERCAST);
	const float darkness = 1.0f - current->get_param(WeatherProfile::PARAM_SCENE_BRIGHTNESS);
	const float fog_density = current->get_param(WeatherProfile::PARAM_FOG_DENSITY);
	const float distance_fog = current->get_param(WeatherProfile::PARAM_DISTANCE_FOG);
	const float sun_through_clouds = p_weather->get_sun_through_clouds();

	// Storm clouds dim the sky light. Through the background energy, the
	// visible sky, ambient light, reflections and GI all dim together.
	const float sky_light = 1.0f - storm_sky_dimming * cloud_darkness;
	const float background_energy = p_weather->get_base(env, SNAME("background_energy_multiplier"));
	p_weather->drive(env, SNAME("background_energy_multiplier"), background_energy * sky_light);

	// Light of the sky near the horizon (linear), at the dimmed brightness.
	const Color overcast_grey = p_weather->get_sky_overcast_color().srgb_to_linear() * (1.0f - 0.65f * cloud_darkness);
	Color horizon = p_weather->get_sky_horizon_color().srgb_to_linear().lerp(overcast_grey, overcast) * (background_energy * sky_light * p_weather->get_sky_energy());
	horizon.a = 1.0f;
	p_weather->set_horizon_light(horizon);

	// Weather fog adds to the scene's volumetric fog. Uniform fog would hide
	// the cloud deck entirely, so it covers the sky less under clouds.
	p_weather->drive(env, SNAME("volumetric_fog_density"), (float)p_weather->get_base(env, SNAME("volumetric_fog_density")) + fog_density);
	p_weather->drive(env, SNAME("volumetric_fog_sky_affect"), (float)p_weather->get_base(env, SNAME("volumetric_fog_sky_affect")) * Math::lerp(1.0f, 0.1f, 1.0f - sun_through_clouds));
	// Fog is lit by the sky like real haze, so only tint it grey.
	const Color base_albedo = p_weather->get_base(env, SNAME("volumetric_fog_albedo"));
	p_weather->drive(env, SNAME("volumetric_fog_albedo"), base_albedo.lerp(storm_fog_color, darkness * 0.5f));
	// Overcast light comes from everywhere: no bright glow toward the sun.
	p_weather->drive(env, SNAME("volumetric_fog_anisotropy"), (float)p_weather->get_base(env, SNAME("volumetric_fog_anisotropy")) * sun_through_clouds);

	// Distance fog is an unlit colour: give it the horizon's colour at the
	// sky's brightness so far hills fade into the horizon instead of going
	// black. The property is sRGB; the brightness math is done in linear.
	const bool base_fog = p_weather->get_base(env, SNAME("fog_enabled"));
	const float base_fog_density = p_weather->get_base(env, SNAME("fog_density"));
	p_weather->drive(env, SNAME("fog_enabled"), base_fog || distance_fog > 0.0005f);
	p_weather->drive(env, SNAME("fog_density"), (base_fog ? base_fog_density : 0.0f) + distance_fog);
	if (distance_fog > 0.0005f) {
		p_weather->drive(env, SNAME("fog_light_color"), horizon.linear_to_srgb());
		p_weather->drive(env, SNAME("fog_light_energy"), 1.0f);
		p_weather->drive(env, SNAME("fog_sky_affect"), overcast * 0.5f);
	} else {
		p_weather->drive(env, SNAME("fog_light_color"), p_weather->get_base(env, SNAME("fog_light_color")));
		p_weather->drive(env, SNAME("fog_light_energy"), p_weather->get_base(env, SNAME("fog_light_energy")));
		p_weather->drive(env, SNAME("fog_sky_affect"), p_weather->get_base(env, SNAME("fog_sky_affect")));
	}
}

void WeatherLightingEffect::set_storm_sky_dimming(float p_amount) {
	storm_sky_dimming = CLAMP(p_amount, 0.0f, 1.0f);
	emit_changed();
}

float WeatherLightingEffect::get_storm_sky_dimming() const {
	return storm_sky_dimming;
}

void WeatherLightingEffect::set_storm_fog_color(const Color &p_color) {
	storm_fog_color = p_color;
	emit_changed();
}

Color WeatherLightingEffect::get_storm_fog_color() const {
	return storm_fog_color;
}

void WeatherLightingEffect::set_overcast_shadow_blur(float p_blur) {
	overcast_shadow_blur = MAX(0.0f, p_blur);
	emit_changed();
}

float WeatherLightingEffect::get_overcast_shadow_blur() const {
	return overcast_shadow_blur;
}

void WeatherLightingEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_storm_sky_dimming", "amount"), &WeatherLightingEffect::set_storm_sky_dimming);
	ClassDB::bind_method(D_METHOD("get_storm_sky_dimming"), &WeatherLightingEffect::get_storm_sky_dimming);
	ClassDB::bind_method(D_METHOD("set_storm_fog_color", "color"), &WeatherLightingEffect::set_storm_fog_color);
	ClassDB::bind_method(D_METHOD("get_storm_fog_color"), &WeatherLightingEffect::get_storm_fog_color);
	ClassDB::bind_method(D_METHOD("set_overcast_shadow_blur", "blur"), &WeatherLightingEffect::set_overcast_shadow_blur);
	ClassDB::bind_method(D_METHOD("get_overcast_shadow_blur"), &WeatherLightingEffect::get_overcast_shadow_blur);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "storm_sky_dimming", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_storm_sky_dimming", "get_storm_sky_dimming");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "storm_fog_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_storm_fog_color", "get_storm_fog_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "overcast_shadow_blur", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_overcast_shadow_blur", "get_overcast_shadow_blur");
}
