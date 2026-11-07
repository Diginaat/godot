/**************************************************************************/
/*  weather_profile.cpp                                                   */
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

#include "weather_profile.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/variant/variant_utility.h"

void WeatherProfile::set_display_name(const String &p_name) {
	display_name = p_name;
	emit_changed();
}

String WeatherProfile::get_display_name() const {
	return display_name;
}

void WeatherProfile::set_param(Param p_param, float p_value) {
	ERR_FAIL_INDEX(p_param, PARAM_MAX);
	params[p_param] = p_value;
	emit_changed();
}

float WeatherProfile::get_param(Param p_param) const {
	ERR_FAIL_INDEX_V(p_param, PARAM_MAX, 0.0f);
	return params[p_param];
}

// Properties that are not weather values.
static bool _is_blendable(const PropertyInfo &p_property) {
	if (!(p_property.usage & PROPERTY_USAGE_STORAGE)) {
		return false;
	}
	if (p_property.name.begins_with("resource_") || p_property.name == "script" || p_property.name == "display_name") {
		return false;
	}
	switch (p_property.type) {
		case Variant::FLOAT:
		case Variant::COLOR:
		case Variant::VECTOR2:
		case Variant::VECTOR3:
		case Variant::VECTOR4:
			return true;
		default:
			return false;
	}
}

void WeatherProfile::blend(const Ref<WeatherProfile> &p_a, const Ref<WeatherProfile> &p_b, float p_weight, const Ref<WeatherProfile> &r_into) {
	ERR_FAIL_COND(p_a.is_null() || p_b.is_null() || r_into.is_null());
	List<PropertyInfo> properties;
	p_b->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!_is_blendable(property)) {
			continue;
		}
		bool valid = false;
		Variant from = p_a->get(property.name, &valid);
		const Variant to = p_b->get(property.name);
		if (!valid) {
			// p_a doesn't have it (a script property): start from its default.
			const Ref<Script> script = p_b->get_script();
			valid = script.is_valid() && script->get_property_default_value(property.name, from);
		}
		if (!valid || from.get_type() != to.get_type()) {
			r_into->set(property.name, to);
			continue;
		}
		if (property.type == Variant::FLOAT && property.hint_string.contains("degrees")) {
			const float angle = Math::rad_to_deg(Math::lerp_angle(Math::deg_to_rad((float)from), Math::deg_to_rad((float)to), p_weight));
			r_into->set(property.name, Math::fposmod(angle, 360.0f));
			continue;
		}
		Callable::CallError error;
		r_into->set(property.name, VariantUtilityFunctions::lerp(from, to, p_weight, error));
	}
	r_into->display_name = p_b->display_name;
}

Ref<WeatherProfile> WeatherProfile::blend_new(const Ref<WeatherProfile> &p_a, const Ref<WeatherProfile> &p_b, float p_weight) {
	ERR_FAIL_COND_V(p_b.is_null(), Ref<WeatherProfile>());
	Ref<WeatherProfile> result = p_b->duplicate();
	blend(p_a, p_b, p_weight, result);
	return result;
}

void WeatherProfile::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &WeatherProfile::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &WeatherProfile::get_display_name);
	ClassDB::bind_method(D_METHOD("set_param", "param", "value"), &WeatherProfile::set_param);
	ClassDB::bind_method(D_METHOD("get_param", "param"), &WeatherProfile::get_param);
	ClassDB::bind_static_method("WeatherProfile", D_METHOD("blend", "from", "to", "weight"), &WeatherProfile::blend_new);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");

	ADD_GROUP("Clouds", "");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "cloud_coverage", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_CLOUD_COVERAGE);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "cloud_density", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_CLOUD_DENSITY);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "cloud_darkness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_CLOUD_DARKNESS);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "overcast", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_OVERCAST);

	ADD_GROUP("Precipitation", "");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "rain_intensity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_RAIN_INTENSITY);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "lightning_per_minute", PROPERTY_HINT_RANGE, "0,30,0.1"), "set_param", "get_param", PARAM_LIGHTNING_PER_MINUTE);

	ADD_GROUP("Wind", "");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "wind_speed", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:m/s"), "set_param", "get_param", PARAM_WIND_SPEED);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "wind_direction_degrees", PROPERTY_HINT_RANGE, "0,360,0.1,degrees"), "set_param", "get_param", PARAM_WIND_DIRECTION_DEGREES);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "gustiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_GUSTINESS);

	ADD_GROUP("Atmosphere", "");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "fog_density", PROPERTY_HINT_RANGE, "0,0.2,0.001"), "set_param", "get_param", PARAM_FOG_DENSITY);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "distance_fog", PROPERTY_HINT_RANGE, "0,0.1,0.0001"), "set_param", "get_param", PARAM_DISTANCE_FOG);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "sun_energy_scale", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_param", "get_param", PARAM_SUN_ENERGY_SCALE);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "scene_brightness", PROPERTY_HINT_RANGE, "0.1,1,0.01"), "set_param", "get_param", PARAM_SCENE_BRIGHTNESS);

	BIND_ENUM_CONSTANT(PARAM_CLOUD_COVERAGE);
	BIND_ENUM_CONSTANT(PARAM_CLOUD_DENSITY);
	BIND_ENUM_CONSTANT(PARAM_CLOUD_DARKNESS);
	BIND_ENUM_CONSTANT(PARAM_OVERCAST);
	BIND_ENUM_CONSTANT(PARAM_RAIN_INTENSITY);
	BIND_ENUM_CONSTANT(PARAM_LIGHTNING_PER_MINUTE);
	BIND_ENUM_CONSTANT(PARAM_WIND_SPEED);
	BIND_ENUM_CONSTANT(PARAM_WIND_DIRECTION_DEGREES);
	BIND_ENUM_CONSTANT(PARAM_GUSTINESS);
	BIND_ENUM_CONSTANT(PARAM_FOG_DENSITY);
	BIND_ENUM_CONSTANT(PARAM_DISTANCE_FOG);
	BIND_ENUM_CONSTANT(PARAM_SUN_ENERGY_SCALE);
	BIND_ENUM_CONSTANT(PARAM_SCENE_BRIGHTNESS);
	BIND_ENUM_CONSTANT(PARAM_MAX);
}

WeatherProfile::WeatherProfile() {
	// Clear weather.
	params[PARAM_CLOUD_COVERAGE] = 0.2f;
	params[PARAM_CLOUD_DENSITY] = 0.8f;
	params[PARAM_CLOUD_DARKNESS] = 0.0f;
	params[PARAM_OVERCAST] = 0.0f;
	params[PARAM_RAIN_INTENSITY] = 0.0f;
	params[PARAM_LIGHTNING_PER_MINUTE] = 0.0f;
	params[PARAM_WIND_SPEED] = 3.0f;
	params[PARAM_WIND_DIRECTION_DEGREES] = 30.0f;
	params[PARAM_GUSTINESS] = 0.2f;
	params[PARAM_FOG_DENSITY] = 0.0f;
	params[PARAM_DISTANCE_FOG] = 0.0f;
	params[PARAM_SUN_ENERGY_SCALE] = 1.0f;
	params[PARAM_SCENE_BRIGHTNESS] = 1.0f;
}
