/**************************************************************************/
/*  weather_effect.cpp                                                    */
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

#include "weather_effect.h"

#include "weather.h"

#include "core/object/class_db.h"

void WeatherEffect::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	emit_changed();
}

bool WeatherEffect::is_enabled() const {
	return enabled;
}

void WeatherEffect::start(Weather *p_weather) {
	GDVIRTUAL_CALL(_start, p_weather);
}

void WeatherEffect::stop(Weather *p_weather) {
	GDVIRTUAL_CALL(_stop, p_weather);
}

void WeatherEffect::update(Weather *p_weather, double p_delta) {
	GDVIRTUAL_CALL(_update, p_weather, p_delta);
}

void WeatherEffect::apply_quality(Weather *p_weather, int p_quality) {
	GDVIRTUAL_CALL(_apply_quality, p_weather, p_quality);
}

void WeatherEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &WeatherEffect::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &WeatherEffect::is_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	GDVIRTUAL_BIND(_start, "weather");
	GDVIRTUAL_BIND(_stop, "weather");
	GDVIRTUAL_BIND(_update, "weather", "delta");
	GDVIRTUAL_BIND(_apply_quality, "weather", "quality");
}

WeatherEffect::WeatherEffect() {
	set_local_to_scene(true);
}
