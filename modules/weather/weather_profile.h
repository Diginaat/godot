/**************************************************************************/
/*  weather_profile.h                                                     */
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

#include "core/io/resource.h"

// One weather state: clouds, rain, wind, fog and light. Weather blends
// between profiles over time.
//
// To add a parameter: add it to Param (before PARAM_MAX), give it a default in
// the constructor and an ADD_PROPERTYI line in _bind_methods(). Blending picks
// it up by itself. Script subclasses can also add exported float, Color and
// vector properties; they are blended too.
class WeatherProfile : public Resource {
	GDCLASS(WeatherProfile, Resource);

public:
	enum Param {
		PARAM_CLOUD_COVERAGE,
		PARAM_CLOUD_DENSITY,
		PARAM_CLOUD_DARKNESS,
		PARAM_OVERCAST,
		PARAM_RAIN_INTENSITY,
		PARAM_LIGHTNING_PER_MINUTE,
		PARAM_WIND_SPEED,
		PARAM_WIND_DIRECTION_DEGREES,
		PARAM_GUSTINESS,
		PARAM_FOG_DENSITY,
		PARAM_DISTANCE_FOG,
		PARAM_SUN_ENERGY_SCALE,
		PARAM_SCENE_BRIGHTNESS,
		PARAM_MAX,
	};

private:
	String display_name = "Clear";
	float params[PARAM_MAX];

protected:
	static void _bind_methods();

public:
	void set_display_name(const String &p_name);
	String get_display_name() const;
	void set_param(Param p_param, float p_value);
	float get_param(Param p_param) const;

	// Interpolates every blendable property from p_a to p_b into r_into
	// (angles in degrees the short way round). r_into must have the same
	// class and script as p_b to receive script properties.
	static void blend(const Ref<WeatherProfile> &p_a, const Ref<WeatherProfile> &p_b, float p_weight, const Ref<WeatherProfile> &r_into);
	static Ref<WeatherProfile> blend_new(const Ref<WeatherProfile> &p_a, const Ref<WeatherProfile> &p_b, float p_weight);

	WeatherProfile();
};

VARIANT_ENUM_CAST(WeatherProfile::Param);
