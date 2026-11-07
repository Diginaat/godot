/**************************************************************************/
/*  weather_sky_effect.cpp                                                */
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

#include "weather_sky_effect.h"

#include "weather_sky_shader.h"

#include "../weather.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_2d.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"

// The weather uniforms the effect sets; a sky shader that has the first one
// is driven instead of replaced.
static const char *weather_uniforms[] = { "cloud_coverage", "cloud_density", "cloud_darkness", "overcast", "cloud_offset" };
static const char *lightning_uniforms[] = { "lightning_flash", "lightning_direction", "lightning_color" };

Ref<ShaderMaterial> WeatherSkyEffect::create_default_material() {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(weather_sky_shader_code);

	Ref<FastNoiseLite> shape_noise;
	shape_noise.instantiate();
	shape_noise->set_seed(7);
	shape_noise->set_frequency(0.006f);
	shape_noise->set_fractal_gain(0.55f);
	Ref<NoiseTexture2D> shape;
	shape.instantiate();
	shape->set_noise(shape_noise);
	shape->set_seamless(true);

	Ref<FastNoiseLite> detail_noise;
	detail_noise.instantiate();
	detail_noise->set_seed(21);
	detail_noise->set_frequency(0.03f);
	detail_noise->set_fractal_octaves(3);
	Ref<NoiseTexture2D> detail;
	detail.instantiate();
	detail->set_width(256);
	detail->set_height(256);
	detail->set_noise(detail_noise);
	detail->set_seamless(true);

	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	// Every parameter is set, so it can be read back without asking the
	// rendering server for the shader's defaults.
	material->set_shader_parameter("sky_top_color", Color(0.22f, 0.38f, 0.66f));
	material->set_shader_parameter("sky_horizon_color", Color(0.62f, 0.68f, 0.76f));
	material->set_shader_parameter("ground_color", Color(0.11f, 0.11f, 0.12f));
	material->set_shader_parameter("overcast_color", Color(0.46f, 0.48f, 0.52f));
	material->set_shader_parameter("horizon_falloff", 0.5f);
	material->set_shader_parameter("sky_energy", 1.0f);
	material->set_shader_parameter("sun_disk_degrees", 0.75f);
	material->set_shader_parameter("sun_disk_energy", 12.0f);
	material->set_shader_parameter("cloud_shape", shape);
	material->set_shader_parameter("cloud_detail", detail);
	material->set_shader_parameter("cloud_altitude", 1500.0f);
	material->set_shader_parameter("cloud_scale", 0.00008f);
	material->set_shader_parameter("detail_scale", 6.0f);
	material->set_shader_parameter("cloud_softness", 0.2f);
	material->set_shader_parameter("cloud_erosion", 0.55f);
	material->set_shader_parameter("cloud_contrast", 0.8f);
	material->set_shader_parameter("cloud_lit_color", Color(1.0f, 0.98f, 0.95f));
	material->set_shader_parameter("cloud_shadow_color", Color(0.56f, 0.6f, 0.68f));
	material->set_shader_parameter("storm_color", Color(0.17f, 0.18f, 0.2f));
	material->set_shader_parameter("cloud_light_energy", 0.55f);
	material->set_shader_parameter("cloud_coverage", 0.2f);
	material->set_shader_parameter("cloud_density", 0.7f);
	material->set_shader_parameter("cloud_darkness", 0.0f);
	material->set_shader_parameter("overcast", 0.0f);
	material->set_shader_parameter("cloud_offset", Vector2());
	material->set_shader_parameter("lightning_flash", 0.0f);
	material->set_shader_parameter("lightning_direction", Vector3(0.0f, 1.0f, 0.0f));
	material->set_shader_parameter("lightning_color", Color(0.75f, 0.82f, 1.0f));
	return material;
}

bool WeatherSkyEffect::_has_uniform(const Ref<ShaderMaterial> &p_material, const StringName &p_name) {
	if (p_material.is_null() || p_material->get_shader().is_null()) {
		return false;
	}
	List<PropertyInfo> uniforms;
	p_material->get_shader()->get_shader_uniform_list(&uniforms);
	for (const PropertyInfo &uniform : uniforms) {
		if (uniform.name == p_name) {
			return true;
		}
	}
	return false;
}

void WeatherSkyEffect::start(Weather *p_weather) {
	environment = p_weather->get_environment();
	saved_horizon_color = p_weather->get_sky_horizon_color();
	saved_overcast_color = p_weather->get_sky_overcast_color();
	saved_sky_energy = p_weather->get_sky_energy();
	if (environment.is_valid()) {
		const Ref<Sky> scene_sky = environment->get_sky();
		const Ref<Material> scene_material = scene_sky.is_valid() ? scene_sky->get_material() : Ref<Material>();
		if (environment->get_background() == Environment::BG_SKY && _has_uniform(scene_material, StringName(weather_uniforms[0]))) {
			material = scene_material;
		} else {
			material = create_default_material();
			// Keep the colours of a procedural sky, so the weather starts
			// from the scene's look.
			const Ref<ProceduralSkyMaterial> procedural = scene_material;
			if (procedural.is_valid()) {
				material->set_shader_parameter("sky_top_color", procedural->get_sky_top_color());
				material->set_shader_parameter("sky_horizon_color", procedural->get_sky_horizon_color());
				material->set_shader_parameter("ground_color", procedural->get_ground_bottom_color());
				material->set_shader_parameter("sky_energy", procedural->get_sky_energy_multiplier());
			}
			own_sky.instantiate();
			own_sky->set_material(material);
			if (scene_sky.is_valid()) {
				own_sky->set_radiance_size(scene_sky->get_radiance_size());
				own_sky->set_process_mode(scene_sky->get_process_mode());
			} else {
				// The clouds move every frame: update the radiance in real time.
				own_sky->set_radiance_size(Sky::RADIANCE_SIZE_256);
				own_sky->set_process_mode(Sky::PROCESS_MODE_REALTIME);
			}
		}
		has_lightning = _has_uniform(material, StringName(lightning_uniforms[0]));
		// The textures repeat every 1 / cloud_scale meters: wrap the drift
		// there, so it never loses precision.
		const Variant scale = material->get_shader_parameter("cloud_scale");
		cloud_period = scale.get_type() == Variant::FLOAT && (float)scale > 0.0f ? 1.0f / (float)scale : 0.0f;
	}
	WeatherEffect::start(p_weather);
}

void WeatherSkyEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	if (environment.is_valid()) {
		if (own_sky.is_valid()) {
			p_weather->release(environment.ptr(), SNAME("sky"));
			p_weather->release(environment.ptr(), SNAME("background_mode"));
		} else if (material.is_valid()) {
			for (const char *uniform : weather_uniforms) {
				p_weather->release(material.ptr(), StringName(String("shader_parameter/") + uniform));
			}
			if (has_lightning) {
				for (const char *uniform : lightning_uniforms) {
					p_weather->release(material.ptr(), StringName(String("shader_parameter/") + uniform));
				}
			}
		}
	}
	p_weather->set_sky_colors(saved_horizon_color, saved_overcast_color, saved_sky_energy);
	environment.unref();
	material.unref();
	own_sky.unref();
	has_lightning = false;
}

void WeatherSkyEffect::_set_parameter(Weather *p_weather, const StringName &p_name, const StringName &p_property, const Variant &p_value) {
	if (own_sky.is_valid()) {
		// The built-in sky is never saved: set it directly.
		material->set_shader_parameter(p_name, p_value);
	} else {
		p_weather->drive(material.ptr(), p_property, p_value);
	}
}

void WeatherSkyEffect::update(Weather *p_weather, double p_delta) {
	if (material.is_null()) {
		WeatherEffect::update(p_weather, p_delta);
		return;
	}
	if (own_sky.is_valid()) {
		p_weather->drive(environment.ptr(), SNAME("sky"), own_sky);
		p_weather->drive(environment.ptr(), SNAME("background_mode"), (int)Environment::BG_SKY);
	}
	const Ref<WeatherProfile> current = p_weather->get_current();

	// Wind aloft is stronger and turned (the Ekman spiral). The shader adds
	// the offset to its lookup, which moves the clouds the opposite way:
	// subtract, so they drift with the wind.
	const float heading = Math::deg_to_rad(current->get_param(WeatherProfile::PARAM_WIND_DIRECTION_DEGREES) + cloud_wind_veer_degrees);
	const Vector2 cloud_wind = Vector2(Math::sin(heading), -Math::cos(heading)) * current->get_param(WeatherProfile::PARAM_WIND_SPEED) * cloud_wind_multiplier;
	cloud_offset -= cloud_wind * (float)p_delta;
	if (cloud_period > 0.0f) {
		cloud_offset = Vector2(Math::fposmod(cloud_offset.x, cloud_period), Math::fposmod(cloud_offset.y, cloud_period));
	}

	_set_parameter(p_weather, SNAME("cloud_coverage"), SNAME("shader_parameter/cloud_coverage"), current->get_param(WeatherProfile::PARAM_CLOUD_COVERAGE));
	_set_parameter(p_weather, SNAME("cloud_density"), SNAME("shader_parameter/cloud_density"), current->get_param(WeatherProfile::PARAM_CLOUD_DENSITY));
	_set_parameter(p_weather, SNAME("cloud_darkness"), SNAME("shader_parameter/cloud_darkness"), current->get_param(WeatherProfile::PARAM_CLOUD_DARKNESS));
	_set_parameter(p_weather, SNAME("overcast"), SNAME("shader_parameter/overcast"), current->get_param(WeatherProfile::PARAM_OVERCAST));
	_set_parameter(p_weather, SNAME("cloud_offset"), SNAME("shader_parameter/cloud_offset"), cloud_offset);

	// Effects after this one (lighting, haze) take the sky's colours.
	const Variant horizon = material->get_shader_parameter(SNAME("sky_horizon_color"));
	const Variant overcast_grey = material->get_shader_parameter(SNAME("overcast_color"));
	const Variant energy = material->get_shader_parameter(SNAME("sky_energy"));
	p_weather->set_sky_colors(horizon.get_type() == Variant::COLOR ? (Color)horizon : saved_horizon_color,
			overcast_grey.get_type() == Variant::COLOR ? (Color)overcast_grey : saved_overcast_color,
			energy.get_type() == Variant::FLOAT ? (float)energy : saved_sky_energy);

	WeatherEffect::update(p_weather, p_delta);
}

Ref<ShaderMaterial> WeatherSkyEffect::get_sky_material() const {
	return material;
}

Vector2 WeatherSkyEffect::get_cloud_offset() const {
	return cloud_offset;
}

void WeatherSkyEffect::set_lightning(Weather *p_weather, float p_flash, const Vector3 &p_direction, const Color &p_color) {
	// A scene sky without the uniforms just doesn't flash.
	if (!has_lightning || !p_weather) {
		return;
	}
	_set_parameter(p_weather, SNAME("lightning_flash"), SNAME("shader_parameter/lightning_flash"), p_flash);
	_set_parameter(p_weather, SNAME("lightning_direction"), SNAME("shader_parameter/lightning_direction"), p_direction);
	_set_parameter(p_weather, SNAME("lightning_color"), SNAME("shader_parameter/lightning_color"), p_color);
}

void WeatherSkyEffect::set_cloud_wind_multiplier(float p_multiplier) {
	cloud_wind_multiplier = MAX(0.0f, p_multiplier);
	emit_changed();
}

float WeatherSkyEffect::get_cloud_wind_multiplier() const {
	return cloud_wind_multiplier;
}

void WeatherSkyEffect::set_cloud_wind_veer_degrees(float p_degrees) {
	cloud_wind_veer_degrees = p_degrees;
	emit_changed();
}

float WeatherSkyEffect::get_cloud_wind_veer_degrees() const {
	return cloud_wind_veer_degrees;
}

void WeatherSkyEffect::_bind_methods() {
	ClassDB::bind_static_method("WeatherSkyEffect", D_METHOD("create_default_material"), &WeatherSkyEffect::create_default_material);
	ClassDB::bind_method(D_METHOD("set_cloud_wind_multiplier", "multiplier"), &WeatherSkyEffect::set_cloud_wind_multiplier);
	ClassDB::bind_method(D_METHOD("get_cloud_wind_multiplier"), &WeatherSkyEffect::get_cloud_wind_multiplier);
	ClassDB::bind_method(D_METHOD("set_cloud_wind_veer_degrees", "degrees"), &WeatherSkyEffect::set_cloud_wind_veer_degrees);
	ClassDB::bind_method(D_METHOD("get_cloud_wind_veer_degrees"), &WeatherSkyEffect::get_cloud_wind_veer_degrees);
	ClassDB::bind_method(D_METHOD("get_sky_material"), &WeatherSkyEffect::get_sky_material);
	ClassDB::bind_method(D_METHOD("get_cloud_offset"), &WeatherSkyEffect::get_cloud_offset);
	ClassDB::bind_method(D_METHOD("set_lightning", "weather", "flash", "direction", "color"), &WeatherSkyEffect::set_lightning);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_wind_multiplier", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_cloud_wind_multiplier", "get_cloud_wind_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_wind_veer_degrees", PROPERTY_HINT_RANGE, "-45,45,0.1,degrees"), "set_cloud_wind_veer_degrees", "get_cloud_wind_veer_degrees");
}
