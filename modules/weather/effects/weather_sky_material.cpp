/**************************************************************************/
/*  weather_sky_material.cpp                                              */
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

#include "weather_sky_material.h"

#include "weather_sky_shader.h"

#include "core/object/class_db.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_2d.h"

static Ref<Shader> builtin_shader;

void WeatherSkyMaterial::register_builtin_shader() {
	builtin_shader.instantiate();
	builtin_shader->set_code(weather_sky_shader_code);
	builtin_shader->set_path(SHADER_PATH, true);
}

void WeatherSkyMaterial::free_builtin_shader() {
	builtin_shader.unref();
}

Ref<Shader> WeatherSkyMaterial::get_builtin_shader() {
	return builtin_shader;
}

void WeatherSkyMaterial::_bind_methods() {
	// The constructor sets the shader and the noise textures. ClassDB would
	// keep them as default values until exit (after the RenderingServer is
	// gone, leaking their RIDs) and warn about object defaults. Null
	// defaults keep them out, and the material always saves them.
	ClassDB::set_property_default_value(get_class_static(), "shader", Variant());
	ClassDB::set_property_default_value(get_class_static(), "shader_parameter/cloud_shape", Variant());
	ClassDB::set_property_default_value(get_class_static(), "shader_parameter/cloud_detail", Variant());
}

WeatherSkyMaterial::WeatherSkyMaterial() {
	set_shader(builtin_shader);

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

	// Every parameter is set, so it can be read back without asking the
	// rendering server for the shader's defaults.
	set_shader_parameter("sky_top_color", Color(0.22f, 0.38f, 0.66f));
	set_shader_parameter("sky_horizon_color", Color(0.62f, 0.68f, 0.76f));
	set_shader_parameter("ground_color", Color(0.11f, 0.11f, 0.12f));
	set_shader_parameter("overcast_color", Color(0.46f, 0.48f, 0.52f));
	set_shader_parameter("horizon_falloff", 0.5f);
	set_shader_parameter("sky_energy", 1.0f);
	set_shader_parameter("sun_disk_degrees", 0.75f);
	set_shader_parameter("sun_disk_energy", 12.0f);
	set_shader_parameter("cloud_shape", shape);
	set_shader_parameter("cloud_detail", detail);
	set_shader_parameter("cloud_altitude", 1500.0f);
	set_shader_parameter("cloud_scale", 0.00008f);
	set_shader_parameter("detail_scale", 6.0f);
	set_shader_parameter("cloud_softness", 0.2f);
	set_shader_parameter("cloud_erosion", 0.55f);
	set_shader_parameter("cloud_contrast", 0.8f);
	set_shader_parameter("cloud_lit_color", Color(1.0f, 0.98f, 0.95f));
	set_shader_parameter("cloud_shadow_color", Color(0.56f, 0.6f, 0.68f));
	set_shader_parameter("storm_color", Color(0.17f, 0.18f, 0.2f));
	set_shader_parameter("cloud_light_energy", 0.55f);
	set_shader_parameter("cloud_coverage", 0.2f);
	set_shader_parameter("cloud_density", 0.7f);
	set_shader_parameter("cloud_darkness", 0.0f);
	set_shader_parameter("overcast", 0.0f);
	set_shader_parameter("cloud_offset", Vector2());
	set_shader_parameter("lightning_flash", 0.0f);
	set_shader_parameter("lightning_direction", Vector3(0.0f, 1.0f, 0.0f));
	set_shader_parameter("lightning_color", Color(0.75f, 0.82f, 1.0f));
}
