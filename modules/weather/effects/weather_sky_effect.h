/**************************************************************************/
/*  weather_sky_effect.h                                                  */
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

#include "../weather_effect.h"

#include "core/math/color.h"
#include "core/math/vector2.h"

class Environment;
class ShaderMaterial;
class Sky;

// Sky and clouds for the weather. If the environment's sky already uses a
// shader with the weather uniforms (cloud_coverage, ...), that material is
// driven; otherwise the effect shows the built-in weather sky in its place
// while it runs. Clouds drift with the wind aloft: stronger than at the
// ground and turned by a few degrees.
class WeatherSkyEffect : public WeatherEffect {
	GDCLASS(WeatherSkyEffect, WeatherEffect);

	float cloud_wind_multiplier = 3.0f;
	float cloud_wind_veer_degrees = 15.0f;

	Ref<Environment> environment;
	Ref<ShaderMaterial> material;
	// Set when the built-in sky replaces the scene's.
	Ref<Sky> own_sky;
	Vector2 cloud_offset;
	float cloud_period = 0.0f;
	Color saved_horizon_color;
	Color saved_overcast_color;
	float saved_sky_energy = 1.0f;

	static bool _has_weather_uniforms(const Ref<ShaderMaterial> &p_material);
	void _set_parameter(Weather *p_weather, const StringName &p_name, const StringName &p_property, const Variant &p_value);

protected:
	static void _bind_methods();

public:
	static Ref<ShaderMaterial> create_default_material();

	void set_cloud_wind_multiplier(float p_multiplier);
	float get_cloud_wind_multiplier() const;
	void set_cloud_wind_veer_degrees(float p_degrees);
	float get_cloud_wind_veer_degrees() const;

	Ref<ShaderMaterial> get_sky_material() const;
	Vector2 get_cloud_offset() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
};
