/**************************************************************************/
/*  weather_library.cpp                                                   */
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

#include "weather_library.h"

#include "core/object/class_db.h"

Ref<WeatherLibrary> WeatherLibrary::default_library;

void WeatherLibrary::set_profiles(const TypedArray<WeatherProfile> &p_profiles) {
	profiles = p_profiles;
	emit_changed();
}

TypedArray<WeatherProfile> WeatherLibrary::get_profiles() const {
	return profiles;
}

Ref<WeatherProfile> WeatherLibrary::find(const String &p_name) const {
	for (int i = 0; i < profiles.size(); i++) {
		Ref<WeatherProfile> profile = profiles[i];
		if (profile.is_valid() && profile->get_display_name() == p_name) {
			return profile;
		}
	}
	return Ref<WeatherProfile>();
}

PackedStringArray WeatherLibrary::get_names() const {
	PackedStringArray names;
	for (int i = 0; i < profiles.size(); i++) {
		Ref<WeatherProfile> profile = profiles[i];
		if (profile.is_valid()) {
			names.push_back(profile->get_display_name());
		}
	}
	return names;
}

int WeatherLibrary::get_count() const {
	return profiles.size();
}

Ref<WeatherLibrary> WeatherLibrary::get_default() {
	if (default_library.is_null()) {
		default_library = create_default();
	}
	return default_library;
}

void WeatherLibrary::free_default() {
	default_library.unref();
}

Ref<WeatherLibrary> WeatherLibrary::create_default() {
	struct Preset {
		const char *name;
		// Cloud coverage, density, darkness, overcast, rain, lightning per
		// minute, wind speed, wind direction, gustiness, fog, distance fog,
		// sun energy, scene brightness (WeatherProfile::Param order).
		float values[WeatherProfile::PARAM_MAX];
	};
	static const Preset presets[] = {
		{ "Clear", { 0.15f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 30.0f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f } },
		{ "Partly Cloudy", { 0.45f, 0.85f, 0.05f, 0.0f, 0.0f, 0.0f, 4.0f, 40.0f, 0.25f, 0.0f, 0.0f, 0.9f, 0.95f } },
		{ "Windy", { 0.45f, 0.8f, 0.1f, 0.1f, 0.0f, 0.0f, 13.0f, 250.0f, 0.8f, 0.0f, 0.0005f, 0.95f, 1.0f } },
		{ "Overcast", { 0.85f, 0.95f, 0.4f, 0.6f, 0.0f, 0.0f, 6.0f, 50.0f, 0.3f, 0.008f, 0.0015f, 0.3f, 0.75f } },
		{ "Mist", { 0.4f, 0.6f, 0.05f, 0.35f, 0.0f, 0.0f, 1.5f, 30.0f, 0.1f, 0.02f, 0.003f, 0.7f, 0.9f } },
		{ "Fog", { 0.9f, 0.9f, 0.2f, 0.85f, 0.0f, 0.0f, 1.0f, 20.0f, 0.05f, 0.07f, 0.012f, 0.25f, 0.75f } },
		{ "Drizzle", { 1.0f, 0.95f, 0.35f, 0.8f, 0.15f, 0.0f, 3.0f, 45.0f, 0.2f, 0.02f, 0.004f, 0.25f, 0.7f } },
		{ "Light Rain", { 0.9f, 0.95f, 0.55f, 0.7f, 0.35f, 0.0f, 6.0f, 55.0f, 0.35f, 0.015f, 0.003f, 0.2f, 0.65f } },
		{ "Heavy Rain", { 1.0f, 1.0f, 0.7f, 0.9f, 0.75f, 0.0f, 8.0f, 60.0f, 0.45f, 0.025f, 0.006f, 0.1f, 0.5f } },
		{ "Storm", { 1.0f, 1.0f, 0.9f, 0.92f, 1.0f, 0.0f, 14.0f, 70.0f, 0.7f, 0.03f, 0.007f, 0.06f, 0.42f } },
		{ "Thunderstorm", { 1.0f, 1.0f, 0.95f, 0.95f, 1.0f, 6.0f, 16.0f, 75.0f, 0.85f, 0.03f, 0.008f, 0.04f, 0.38f } },
	};
	Ref<WeatherLibrary> library;
	library.instantiate();
	TypedArray<WeatherProfile> list;
	for (const Preset &preset : presets) {
		Ref<WeatherProfile> profile;
		profile.instantiate();
		profile->set_display_name(preset.name);
		for (int i = 0; i < WeatherProfile::PARAM_MAX; i++) {
			profile->set_param(WeatherProfile::Param(i), preset.values[i]);
		}
		list.push_back(profile);
	}
	library->set_profiles(list);
	return library;
}

void WeatherLibrary::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_profiles", "profiles"), &WeatherLibrary::set_profiles);
	ClassDB::bind_method(D_METHOD("get_profiles"), &WeatherLibrary::get_profiles);
	ClassDB::bind_method(D_METHOD("find", "name"), &WeatherLibrary::find);
	ClassDB::bind_method(D_METHOD("get_names"), &WeatherLibrary::get_names);
	ClassDB::bind_static_method("WeatherLibrary", D_METHOD("get_default"), &WeatherLibrary::get_default);
	ClassDB::bind_static_method("WeatherLibrary", D_METHOD("create_default"), &WeatherLibrary::create_default);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "profiles", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("WeatherProfile")), "set_profiles", "get_profiles");
}
