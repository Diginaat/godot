/**************************************************************************/
/*  weather_lighting_effect.h                                             */
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

// Sun, sky light, exposure and fog for the weather: clouds dim and soften
// the sun (soft shadows under an overcast, no glint on wet floors), storm
// clouds darken the sky light, thick clouds lower the exposure, and distant
// fog takes the horizon's colour.
class WeatherLightingEffect : public WeatherEffect {
	GDCLASS(WeatherLightingEffect, WeatherEffect);

	float storm_sky_dimming = 0.5f;
	Color storm_fog_color = Color(0.16f, 0.17f, 0.19f);
	float overcast_shadow_blur = 6.0f;

	void _apply_sun(Weather *p_weather);
	void _apply_exposure(Weather *p_weather);
	void _apply_environment(Weather *p_weather);

protected:
	static void _bind_methods();

public:
	void set_storm_sky_dimming(float p_amount);
	float get_storm_sky_dimming() const;
	void set_storm_fog_color(const Color &p_color);
	Color get_storm_fog_color() const;
	void set_overcast_shadow_blur(float p_blur);
	float get_overcast_shadow_blur() const;

	virtual void update(Weather *p_weather, double p_delta) override;
};
