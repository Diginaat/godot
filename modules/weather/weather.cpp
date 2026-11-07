/**************************************************************************/
/*  weather.cpp                                                           */
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

#include "weather.h"

#include "effects/weather_lighting_effect.h"
#include "wind/wind_server.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/sky.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#endif

const char *Weather::GLOBAL_WETNESS = "weather_wetness";
const char *Weather::GLOBAL_RAIN = "weather_rain";

void Weather::register_project_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::INT, "weather/quality", PROPERTY_HINT_ENUM, "Low,Medium,High,Ultra"), 2);
}

void Weather::register_shader_globals() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	// Surface shaders read these for wet surfaces; see WEATHER.md.
	rs->global_shader_parameter_add(GLOBAL_WETNESS, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0f);
	rs->global_shader_parameter_add(GLOBAL_RAIN, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0f);
}

TypedArray<WeatherEffect> Weather::create_default_effects() {
	TypedArray<WeatherEffect> list;
	list.push_back(memnew(WeatherLightingEffect));
	return list;
}

// Settings.

void Weather::set_library(const Ref<WeatherLibrary> &p_library) {
	library = p_library;
	notify_property_list_changed();
}

Ref<WeatherLibrary> Weather::get_library() const {
	return library;
}

Ref<WeatherLibrary> Weather::get_effective_library() const {
	return library.is_valid() ? library : WeatherLibrary::get_default();
}

void Weather::set_weather(const String &p_name) {
	weather = p_name;
	if (weather == CUSTOM_NAME && custom_profile.is_null()) {
		// Start from what's showing, to tweak from there.
		custom_profile = current.is_valid() ? Ref<WeatherProfile>(current->duplicate()) : Ref<WeatherProfile>(memnew(WeatherProfile));
		custom_profile->set_display_name(CUSTOM_NAME);
	}
	notify_property_list_changed();
	if (is_ready() && !selecting) {
		change_weather(weather, Engine::get_singleton()->is_editor_hint() ? 0.0f : -1.0f);
	}
}

String Weather::get_weather() const {
	return weather;
}

void Weather::set_custom_profile(const Ref<WeatherProfile> &p_profile) {
	custom_profile = p_profile;
}

Ref<WeatherProfile> Weather::get_custom_profile() const {
	return custom_profile;
}

void Weather::set_effects(const TypedArray<WeatherEffect> &p_effects) {
	effects = p_effects;
}

TypedArray<WeatherEffect> Weather::get_effects() const {
	return effects;
}

void Weather::set_transition_time(float p_time) {
	transition_time = MAX(0.0f, p_time);
}

float Weather::get_transition_time() const {
	return transition_time;
}

void Weather::set_gust_direction_swing(float p_degrees) {
	gust_direction_swing = CLAMP(p_degrees, 0.0f, 90.0f);
}

float Weather::get_gust_direction_swing() const {
	return gust_direction_swing;
}

void Weather::set_gust_rain_bursts(float p_amount) {
	gust_rain_bursts = CLAMP(p_amount, 0.0f, 1.0f);
}

float Weather::get_gust_rain_bursts() const {
	return gust_rain_bursts;
}

void Weather::set_quality(Quality p_quality) {
	quality = p_quality;
}

Weather::Quality Weather::get_quality() const {
	return quality;
}

void Weather::set_wetting_time(float p_time) {
	wetting_time = MAX(0.01f, p_time);
}

float Weather::get_wetting_time() const {
	return wetting_time;
}

void Weather::set_drying_time(float p_time) {
	drying_time = MAX(0.01f, p_time);
}

float Weather::get_drying_time() const {
	return drying_time;
}

void Weather::set_world_environment_path(const NodePath &p_path) {
	world_environment_path = p_path;
	if (is_inside_tree()) {
		_find_scene_nodes();
	}
}

NodePath Weather::get_world_environment_path() const {
	return world_environment_path;
}

void Weather::set_sun_path(const NodePath &p_path) {
	sun_path = p_path;
	if (is_inside_tree()) {
		_find_scene_nodes();
	}
}

NodePath Weather::get_sun_path() const {
	return sun_path;
}

void Weather::set_preview_in_editor(bool p_enabled) {
	preview_in_editor = p_enabled;
}

bool Weather::is_preview_in_editor() const {
	return preview_in_editor;
}

// Choosing the weather.

Ref<WeatherProfile> Weather::get_profile(const String &p_name) {
	if (p_name == CUSTOM_NAME) {
		if (custom_profile.is_null()) {
			custom_profile.instantiate();
			custom_profile->set_display_name(CUSTOM_NAME);
		}
		return custom_profile;
	}
	return get_effective_library()->find(p_name);
}

PackedStringArray Weather::get_weather_names() const {
	PackedStringArray names = get_effective_library()->get_names();
	names.push_back(CUSTOM_NAME);
	return names;
}

String Weather::get_weather_name() const {
	if (target.is_valid()) {
		return target->get_display_name();
	}
	return current.is_valid() ? current->get_display_name() : String();
}

void Weather::change_weather(const String &p_name, float p_duration) {
	Ref<WeatherProfile> profile = get_profile(p_name);
	ERR_FAIL_COND_MSG(profile.is_null(), vformat("Weather: no weather type named \"%s\" in the library.", p_name));
	selecting = true;
	weather = p_name;
	selecting = false;
	_set_target(profile, p_duration);
}

void Weather::change_profile(const Ref<WeatherProfile> &p_profile, float p_duration) {
	ERR_FAIL_COND(p_profile.is_null());
	// A library profile selects its type; any other becomes the custom weather.
	String name = CUSTOM_NAME;
	const TypedArray<WeatherProfile> list = get_effective_library()->get_profiles();
	for (int i = 0; i < list.size(); i++) {
		if (Ref<WeatherProfile>(list[i]) == p_profile) {
			name = p_profile->get_display_name();
			break;
		}
	}
	if (name == CUSTOM_NAME) {
		custom_profile = p_profile;
	}
	selecting = true;
	weather = name;
	selecting = false;
	notify_property_list_changed();
	_set_target(p_profile, p_duration);
}

void Weather::next_weather() {
	const PackedStringArray names = get_weather_names();
	int index = names.find(weather);
	index = (index + 1) % names.size();
	if (names[index] == CUSTOM_NAME && custom_profile.is_null()) {
		index = 0;
	}
	change_weather(names[index]);
}

Ref<WeatherProfile> Weather::_match_class(const Ref<WeatherProfile> &p_profile, const Ref<WeatherProfile> &p_into) const {
	// Script profiles carry their own properties: blend into the same kind.
	if (p_into.is_null() || p_into->get_class_name() != p_profile->get_class_name() || p_into->get_script() != p_profile->get_script()) {
		return p_profile->duplicate();
	}
	return p_into;
}

void Weather::_set_target(const Ref<WeatherProfile> &p_profile, float p_duration) {
	if (p_duration < 0.0f) {
		p_duration = transition_time;
	}
	target = p_profile;
	if (current->get_class_name() == target->get_class_name() && current->get_script() == target->get_script()) {
		from = current->duplicate();
	} else {
		// A different kind of profile (a script one with its own values):
		// start from its defaults, with the values that are showing now.
		Object *fresh = ClassDB::instantiate(target->get_class_name());
		from = Ref<WeatherProfile>(Object::cast_to<WeatherProfile>(fresh));
		if (from.is_null()) {
			if (fresh) {
				memdelete(fresh);
			}
			from = current->duplicate();
		} else {
			from->set_script(target->get_script());
			WeatherProfile::blend(current, current, 1.0f, from);
		}
	}
	current = _match_class(target, current);
	if (p_duration <= 0.0f) {
		progress = 1.0f;
		WeatherProfile::blend(target, target, 1.0f, current);
		wetness = target->get_param(WeatherProfile::PARAM_RAIN_INTENSITY);
	} else {
		progress = 0.0f;
		duration = p_duration;
	}
	emit_signal(SNAME("weather_changed"), target);
}

// State for effects.

Ref<WeatherProfile> Weather::get_current() const {
	return current;
}

Ref<WeatherProfile> Weather::get_target() const {
	return target;
}

float Weather::get_wetness() const {
	return wetness;
}

void Weather::set_wetness(float p_wetness) {
	wetness = CLAMP(p_wetness, 0.0f, 1.0f);
}

float Weather::get_rain_amount() const {
	return rain_amount;
}

float Weather::get_gust() const {
	return gust;
}

Vector3 Weather::get_wind() const {
	return wind;
}

Color Weather::get_horizon_light() const {
	return horizon_light;
}

float Weather::get_sun_through_clouds() const {
	// How much direct sunlight still gets below the clouds.
	const float overcast = current->get_param(WeatherProfile::PARAM_OVERCAST);
	const float coverage = current->get_param(WeatherProfile::PARAM_CLOUD_COVERAGE);
	const float density = current->get_param(WeatherProfile::PARAM_CLOUD_DENSITY);
	return (1.0f - overcast) * (1.0f - 0.6f * coverage * density);
}

double Weather::get_time() const {
	return time;
}

Weather::Quality Weather::get_effective_quality() const {
	if (quality != QUALITY_AUTO) {
		return quality;
	}
	const int setting = GLOBAL_GET("weather/quality");
	return Quality(CLAMP(setting + QUALITY_LOW, (int)QUALITY_LOW, (int)QUALITY_ULTRA));
}

bool Weather::is_previewing() const {
	return previewing;
}

Camera3D *Weather::get_view_camera() const {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		EditorInterface *editor = EditorInterface::get_singleton();
		SubViewport *viewport = editor ? editor->get_editor_viewport_3d(0) : nullptr;
		return viewport ? viewport->get_camera_3d() : nullptr;
	}
#endif
	return is_inside_tree() ? get_viewport()->get_camera_3d() : nullptr;
}

WorldEnvironment *Weather::get_world_environment() const {
	return world_environment;
}

Ref<Environment> Weather::get_environment() const {
	return world_environment ? world_environment->get_environment() : Ref<Environment>();
}

Ref<CameraAttributes> Weather::get_camera_attributes() const {
	return world_environment ? world_environment->get_camera_attributes() : Ref<CameraAttributes>();
}

DirectionalLight3D *Weather::get_sun() const {
	return sun;
}

// Scene values.

Variant Weather::get_base(Object *p_object, const StringName &p_property) {
	ERR_FAIL_NULL_V(p_object, Variant());
	const ObjectID id = p_object->get_instance_id();
	HashMap<StringName, Variant> &object_bases = bases[id];
	HashMap<StringName, Variant> &object_written = written[id];
	const Variant value = p_object->get(p_property);
	HashMap<StringName, Variant>::Iterator last = object_written.find(p_property);
	if (!object_bases.has(p_property) || (last && last->value != value)) {
		object_bases[p_property] = value;
		object_written.erase(p_property);
	}
	return object_bases[p_property];
}

void Weather::drive(Object *p_object, const StringName &p_property, const Variant &p_value) {
	ERR_FAIL_NULL(p_object);
	get_base(p_object, p_property);
	p_object->set(p_property, p_value);
	// Store what reads back: floats and colors may be stored at lower precision.
	written[p_object->get_instance_id()][p_property] = p_object->get(p_property);
}

void Weather::release(Object *p_object, const StringName &p_property) {
	ERR_FAIL_NULL(p_object);
	const ObjectID id = p_object->get_instance_id();
	HashMap<ObjectID, HashMap<StringName, Variant>>::Iterator object_bases = bases.find(id);
	if (object_bases && object_bases->value.has(p_property)) {
		p_object->set(p_property, object_bases->value[p_property]);
		object_bases->value.erase(p_property);
	}
	HashMap<ObjectID, HashMap<StringName, Variant>>::Iterator object_written = written.find(id);
	if (object_written) {
		object_written->value.erase(p_property);
	}
}

void Weather::restore_scene() {
	for (const KeyValue<ObjectID, HashMap<StringName, Variant>> &E : bases) {
		Object *object = ObjectDB::get_instance(E.key);
		if (!object) {
			continue;
		}
		for (const KeyValue<StringName, Variant> &property : E.value) {
			object->set(property.key, property.value);
		}
	}
	bases.clear();
	written.clear();
}

// Running.

void Weather::_find_scene_nodes() {
	world_environment = Object::cast_to<WorldEnvironment>(get_node_or_null(world_environment_path));
	sun = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_path));
	if (world_environment && sun) {
		return;
	}
	Node *root = nullptr;
	if (is_inside_tree()) {
		root = Engine::get_singleton()->is_editor_hint() ? get_tree()->get_edited_scene_root() : get_tree()->get_current_scene();
	}
	if (!root || !root->is_ancestor_of(this)) {
		root = get_parent();
	}
	if (!root) {
		return;
	}
	if (!world_environment) {
		TypedArray<Node> found = root->find_children("*", "WorldEnvironment", true, false);
		world_environment = found.is_empty() ? nullptr : Object::cast_to<WorldEnvironment>(found[0]);
	}
	if (!sun) {
		TypedArray<Node> found = root->find_children("*", "DirectionalLight3D", true, false);
		sun = found.is_empty() ? nullptr : Object::cast_to<DirectionalLight3D>(found[0]);
	}
}

void Weather::_read_sky_colors() {
	const Ref<Environment> environment = get_environment();
	if (environment.is_null()) {
		return;
	}
	if (environment->get_background() == Environment::BG_COLOR) {
		sky_horizon_color = environment->get_bg_color();
		return;
	}
	if (environment->get_sky().is_null()) {
		return;
	}
	const Ref<Material> material = environment->get_sky()->get_material();
	const Ref<ProceduralSkyMaterial> procedural = material;
	if (procedural.is_valid()) {
		sky_horizon_color = procedural->get_sky_horizon_color();
		sky_energy = procedural->get_sky_energy_multiplier();
		return;
	}
	const Ref<ShaderMaterial> shader_material = material;
	if (shader_material.is_valid()) {
		// Only the parameters set on the material: reading shader defaults
		// would stall the render thread.
		const Variant horizon = shader_material->get_shader_parameter("sky_horizon_color");
		if (horizon.get_type() == Variant::COLOR) {
			sky_horizon_color = horizon;
		}
		const Variant overcast_grey = shader_material->get_shader_parameter("overcast_color");
		if (overcast_grey.get_type() == Variant::COLOR) {
			sky_overcast_color = overcast_grey;
		}
		const Variant energy = shader_material->get_shader_parameter("sky_energy");
		if (energy.get_type() == Variant::FLOAT) {
			sky_energy = energy;
		}
	}
}

void Weather::_sync_effects() {
	// Stop effects that were removed from the list or turned off.
	for (int64_t i = (int64_t)running_effects.size() - 1; i >= 0; i--) {
		const Ref<WeatherEffect> &effect = running_effects[i];
		if (!effects.has(effect) || !effect->is_enabled()) {
			effect->stop(this);
			running_effects.remove_at(i);
		}
	}
	const Quality effective_quality = get_effective_quality();
	const bool quality_changed = (int)effective_quality != applied_quality;
	applied_quality = effective_quality;
	for (int i = 0; i < effects.size(); i++) {
		Ref<WeatherEffect> effect = effects[i];
		if (effect.is_null() || !effect->is_enabled()) {
			continue;
		}
		if (running_effects.find(effect) < 0) {
			running_effects.push_back(effect);
			effect->apply_quality(this, effective_quality);
			effect->start(this);
		} else if (quality_changed) {
			effect->apply_quality(this, effective_quality);
		}
	}
}

void Weather::_stop_effects() {
	for (const Ref<WeatherEffect> &effect : running_effects) {
		effect->stop(this);
	}
	running_effects.clear();
}

bool Weather::_update_editor_preview() {
	if (!preview_in_editor) {
		if (previewing) {
			previewing = false;
			_stop_effects();
			restore_scene();
			RenderingServer::get_singleton()->global_shader_parameter_set(GLOBAL_WETNESS, 0.0f);
			RenderingServer::get_singleton()->global_shader_parameter_set(GLOBAL_RAIN, 0.0f);
		}
		return false;
	}
	previewing = true;
	Ref<WeatherProfile> profile = get_profile(weather);
	if (profile.is_null()) {
		return false;
	}
	if (profile != target) {
		_set_target(profile, 0.0f);
	}
	return true;
}

void Weather::_process_weather(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint() && !_update_editor_preview()) {
		return;
	}
	if (target.is_null()) {
		return;
	}
	time += p_delta;
	current = _match_class(target, current);
	if (progress < 1.0f) {
		progress = MIN(progress + (float)p_delta / duration, 1.0f);
		WeatherProfile::blend(from, target, Math::smoothstep(0.0f, 1.0f, progress), current);
	} else {
		// Re-read the target every frame, so edits to it show at once.
		WeatherProfile::blend(target, target, 1.0f, current);
	}

	const float rain = current->get_param(WeatherProfile::PARAM_RAIN_INTENSITY);
	const float gustiness = current->get_param(WeatherProfile::PARAM_GUSTINESS);
	const Camera3D *camera = get_view_camera();
	const Vector3 view_position = camera ? camera->get_global_position() : Vector3();

	WindServer *wind_server = WindServer::get_singleton();
	if (wind_server) {
		drive(wind_server, SNAME("speed"), current->get_param(WeatherProfile::PARAM_WIND_SPEED));
		drive(wind_server, SNAME("direction_degrees"), current->get_param(WeatherProfile::PARAM_WIND_DIRECTION_DEGREES));
		drive(wind_server, SNAME("gustiness"), gustiness);
		drive(wind_server, SNAME("direction_swing_degrees"), gust_direction_swing);
		gust = wind_server->get_gust(view_position);
		wind = wind_server->sample(view_position);
	}
	// Rain comes in bursts with the gusts.
	rain_amount = CLAMP(rain * (1.0f + gust_rain_bursts * gustiness * (gust - 0.5f)), 0.0f, 1.0f);

	// Light of the sky near the horizon (linear): lights haze, rain and
	// distant fog. The lighting effect replaces it with the dimmed sky.
	const float darkness = current->get_param(WeatherProfile::PARAM_CLOUD_DARKNESS);
	const Color overcast_grey = sky_overcast_color.srgb_to_linear() * (1.0f - 0.65f * darkness);
	const Ref<Environment> environment = get_environment();
	const float energy = environment.is_valid() ? environment->get_bg_energy_multiplier() : 1.0f;
	horizon_light = sky_horizon_color.srgb_to_linear().lerp(overcast_grey, current->get_param(WeatherProfile::PARAM_OVERCAST)) * (energy * sky_energy);
	horizon_light.a = 1.0f;

	// Surfaces lag behind the rain: they wet over seconds and dry over minutes.
	if (rain > 0.02f) {
		wetness = Math::move_toward(wetness, 1.0f, (float)p_delta / wetting_time * (0.3f + 0.7f * rain));
	} else {
		wetness = Math::move_toward(wetness, 0.0f, (float)p_delta / drying_time);
	}
	RenderingServer::get_singleton()->global_shader_parameter_set(GLOBAL_WETNESS, wetness);
	RenderingServer::get_singleton()->global_shader_parameter_set(GLOBAL_RAIN, rain_amount);

	_sync_effects();
	for (const Ref<WeatherEffect> &effect : running_effects) {
		effect->update(this, p_delta);
	}
}

void Weather::set_horizon_light(const Color &p_color) {
	horizon_light = p_color;
}

Color Weather::get_sky_horizon_color() const {
	return sky_horizon_color;
}

Color Weather::get_sky_overcast_color() const {
	return sky_overcast_color;
}

void Weather::set_sky_colors(const Color &p_horizon, const Color &p_overcast, float p_energy) {
	sky_horizon_color = p_horizon;
	sky_overcast_color = p_overcast;
	sky_energy = p_energy;
}

float Weather::get_sky_energy() const {
	return sky_energy;
}

void Weather::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_find_scene_nodes();
			_read_sky_colors();
			add_to_group(SNAME("weather"));
		} break;
		case NOTIFICATION_READY: {
			Ref<WeatherProfile> profile = get_profile(weather);
			if (profile.is_null()) {
				profile = get_effective_library()->get_count() > 0 ? Ref<WeatherProfile>(get_effective_library()->get_profiles()[0]) : get_profile(CUSTOM_NAME);
			}
			_set_target(profile, 0.0f);
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_process_weather(get_process_delta_time());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// The environment, sun and sky are shared resources: leave them as found.
			_stop_effects();
			restore_scene();
		} break;
		case NOTIFICATION_EDITOR_PRE_SAVE: {
			// Never save the weather into the scene; the next frame applies it again.
			restore_scene();
		} break;
	}
}

void Weather::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "weather") {
		p_property.hint = PROPERTY_HINT_ENUM;
		p_property.hint_string = String(",").join(get_weather_names());
	} else if (p_property.name == "custom_profile" && weather != CUSTOM_NAME) {
		// Only shown with Custom picked (still saved).
		p_property.usage = PROPERTY_USAGE_STORAGE;
	}
}

void Weather::_bind_methods() {
	ClassDB::bind_static_method("Weather", D_METHOD("create_default_effects"), &Weather::create_default_effects);

	ClassDB::bind_method(D_METHOD("set_library", "library"), &Weather::set_library);
	ClassDB::bind_method(D_METHOD("get_library"), &Weather::get_library);
	ClassDB::bind_method(D_METHOD("get_effective_library"), &Weather::get_effective_library);
	ClassDB::bind_method(D_METHOD("set_weather", "name"), &Weather::set_weather);
	ClassDB::bind_method(D_METHOD("get_weather"), &Weather::get_weather);
	ClassDB::bind_method(D_METHOD("set_custom_profile", "profile"), &Weather::set_custom_profile);
	ClassDB::bind_method(D_METHOD("get_custom_profile"), &Weather::get_custom_profile);
	ClassDB::bind_method(D_METHOD("set_effects", "effects"), &Weather::set_effects);
	ClassDB::bind_method(D_METHOD("get_effects"), &Weather::get_effects);
	ClassDB::bind_method(D_METHOD("set_transition_time", "time"), &Weather::set_transition_time);
	ClassDB::bind_method(D_METHOD("get_transition_time"), &Weather::get_transition_time);
	ClassDB::bind_method(D_METHOD("set_gust_direction_swing", "degrees"), &Weather::set_gust_direction_swing);
	ClassDB::bind_method(D_METHOD("get_gust_direction_swing"), &Weather::get_gust_direction_swing);
	ClassDB::bind_method(D_METHOD("set_gust_rain_bursts", "amount"), &Weather::set_gust_rain_bursts);
	ClassDB::bind_method(D_METHOD("get_gust_rain_bursts"), &Weather::get_gust_rain_bursts);
	ClassDB::bind_method(D_METHOD("set_quality", "quality"), &Weather::set_quality);
	ClassDB::bind_method(D_METHOD("get_quality"), &Weather::get_quality);
	ClassDB::bind_method(D_METHOD("set_wetting_time", "time"), &Weather::set_wetting_time);
	ClassDB::bind_method(D_METHOD("get_wetting_time"), &Weather::get_wetting_time);
	ClassDB::bind_method(D_METHOD("set_drying_time", "time"), &Weather::set_drying_time);
	ClassDB::bind_method(D_METHOD("get_drying_time"), &Weather::get_drying_time);
	ClassDB::bind_method(D_METHOD("set_world_environment_path", "path"), &Weather::set_world_environment_path);
	ClassDB::bind_method(D_METHOD("get_world_environment_path"), &Weather::get_world_environment_path);
	ClassDB::bind_method(D_METHOD("set_sun_path", "path"), &Weather::set_sun_path);
	ClassDB::bind_method(D_METHOD("get_sun_path"), &Weather::get_sun_path);
	ClassDB::bind_method(D_METHOD("set_preview_in_editor", "enabled"), &Weather::set_preview_in_editor);
	ClassDB::bind_method(D_METHOD("is_preview_in_editor"), &Weather::is_preview_in_editor);

	ClassDB::bind_method(D_METHOD("change_weather", "name", "duration"), &Weather::change_weather, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("change_profile", "profile", "duration"), &Weather::change_profile, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("next_weather"), &Weather::next_weather);
	ClassDB::bind_method(D_METHOD("get_profile", "name"), &Weather::get_profile);
	ClassDB::bind_method(D_METHOD("get_weather_names"), &Weather::get_weather_names);
	ClassDB::bind_method(D_METHOD("get_weather_name"), &Weather::get_weather_name);
	ClassDB::bind_method(D_METHOD("get_current"), &Weather::get_current);
	ClassDB::bind_method(D_METHOD("get_target"), &Weather::get_target);
	ClassDB::bind_method(D_METHOD("get_wetness"), &Weather::get_wetness);
	ClassDB::bind_method(D_METHOD("set_wetness", "wetness"), &Weather::set_wetness);
	ClassDB::bind_method(D_METHOD("get_rain_amount"), &Weather::get_rain_amount);
	ClassDB::bind_method(D_METHOD("get_gust"), &Weather::get_gust);
	ClassDB::bind_method(D_METHOD("get_wind"), &Weather::get_wind);
	ClassDB::bind_method(D_METHOD("get_horizon_light"), &Weather::get_horizon_light);
	ClassDB::bind_method(D_METHOD("set_horizon_light", "color"), &Weather::set_horizon_light);
	ClassDB::bind_method(D_METHOD("get_sky_horizon_color"), &Weather::get_sky_horizon_color);
	ClassDB::bind_method(D_METHOD("get_sky_overcast_color"), &Weather::get_sky_overcast_color);
	ClassDB::bind_method(D_METHOD("get_sky_energy"), &Weather::get_sky_energy);
	ClassDB::bind_method(D_METHOD("get_sun_through_clouds"), &Weather::get_sun_through_clouds);
	ClassDB::bind_method(D_METHOD("get_time"), &Weather::get_time);
	ClassDB::bind_method(D_METHOD("get_effective_quality"), &Weather::get_effective_quality);
	ClassDB::bind_method(D_METHOD("is_previewing"), &Weather::is_previewing);
	ClassDB::bind_method(D_METHOD("get_view_camera"), &Weather::get_view_camera);
	ClassDB::bind_method(D_METHOD("get_world_environment"), &Weather::get_world_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &Weather::get_environment);
	ClassDB::bind_method(D_METHOD("get_camera_attributes"), &Weather::get_camera_attributes);
	ClassDB::bind_method(D_METHOD("get_sun"), &Weather::get_sun);
	ClassDB::bind_method(D_METHOD("drive", "object", "property", "value"), &Weather::drive);
	ClassDB::bind_method(D_METHOD("get_base", "object", "property"), &Weather::get_base);
	ClassDB::bind_method(D_METHOD("release", "object", "property"), &Weather::release);
	ClassDB::bind_method(D_METHOD("restore_scene"), &Weather::restore_scene);
	ClassDB::bind_method(D_METHOD("set_sky_colors", "horizon", "overcast", "energy"), &Weather::set_sky_colors);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "weather"), "set_weather", "get_weather");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "custom_profile", PROPERTY_HINT_RESOURCE_TYPE, "WeatherProfile"), "set_custom_profile", "get_custom_profile");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "library", PROPERTY_HINT_RESOURCE_TYPE, "WeatherLibrary"), "set_library", "get_library");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effects", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("WeatherEffect")), "set_effects", "get_effects");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transition_time", PROPERTY_HINT_RANGE, "0,120,0.1,or_greater,suffix:s"), "set_transition_time", "get_transition_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "quality", PROPERTY_HINT_ENUM, "Auto,Low,Medium,High,Ultra"), "set_quality", "get_quality");

	ADD_GROUP("Wind", "gust_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_direction_swing", PROPERTY_HINT_RANGE, "0,90,0.1,degrees"), "set_gust_direction_swing", "get_gust_direction_swing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_rain_bursts", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_gust_rain_bursts", "get_gust_rain_bursts");

	ADD_GROUP("Wetness", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetting_time", PROPERTY_HINT_RANGE, "0.1,600,0.1,suffix:s"), "set_wetting_time", "get_wetting_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drying_time", PROPERTY_HINT_RANGE, "0.1,3600,0.1,suffix:s"), "set_drying_time", "get_drying_time");

	ADD_GROUP("Scene", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_environment_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "WorldEnvironment"), "set_world_environment_path", "get_world_environment_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun_path", "get_sun_path");

	ADD_GROUP("Editor", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preview_in_editor"), "set_preview_in_editor", "is_preview_in_editor");

	ADD_SIGNAL(MethodInfo("weather_changed", PropertyInfo(Variant::OBJECT, "profile", PROPERTY_HINT_RESOURCE_TYPE, "WeatherProfile")));

	BIND_ENUM_CONSTANT(QUALITY_AUTO);
	BIND_ENUM_CONSTANT(QUALITY_LOW);
	BIND_ENUM_CONSTANT(QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(QUALITY_HIGH);
	BIND_ENUM_CONSTANT(QUALITY_ULTRA);
}

Weather::Weather() {
	current.instantiate();
	effects = create_default_effects();
}

Weather::~Weather() {
}
