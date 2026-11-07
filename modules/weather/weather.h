/**************************************************************************/
/*  weather.h                                                             */
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

#include "weather_effect.h"
#include "weather_library.h"
#include "weather_profile.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"

class Camera3D;
class CameraAttributes;
class DirectionalLight3D;
class Environment;
class WorldEnvironment;

// The weather of a level. Picks a weather type from its library (or the
// custom profile), blends to it over transition_time, drives the WindServer
// and the surface wetness, and runs its effects (lighting, sky, rain...)
// every frame. Runs in the editor too, so the viewport shows the weather; the
// scene values it changes are restored before the scene is saved.
class Weather : public Node {
	GDCLASS(Weather, Node);

public:
	enum Quality {
		QUALITY_AUTO,
		QUALITY_LOW,
		QUALITY_MEDIUM,
		QUALITY_HIGH,
		QUALITY_ULTRA,
	};

	static constexpr const char *CUSTOM_NAME = "Custom";
	static const char *GLOBAL_WETNESS;
	static const char *GLOBAL_RAIN;
	static const char *GLOBAL_RAIN_OCCLUSION;
	static const char *GLOBAL_OCCLUSION_AREA;
	static constexpr const char *WETNESS_INCLUDE_PATH = "res://.builtin/weather_wetness.gdshaderinc";
	static constexpr const char *WET_SURFACE_SHADER_PATH = "res://.builtin/wet_surface.gdshader";

private:
	Ref<WeatherLibrary> library;
	String weather = "Clear";
	Ref<WeatherProfile> custom_profile;
	TypedArray<WeatherEffect> effects;
	float transition_time = 12.0f;
	float gust_direction_swing = 20.0f;
	float gust_rain_bursts = 0.5f;
	Quality quality = QUALITY_AUTO;
	float wetting_time = 20.0f;
	float drying_time = 90.0f;
	NodePath world_environment_path;
	NodePath sun_path;
	bool preview_in_editor = true;

	Ref<WeatherProfile> current;
	Ref<WeatherProfile> from;
	Ref<WeatherProfile> target;
	float progress = 1.0f;
	float duration = 1.0f;
	bool selecting = false;

	double time = 0.0;
	float wetness = 0.0f;
	float rain_amount = 0.0f;
	float gust = 0.5f;
	Vector3 wind;
	Color horizon_light;
	bool previewing = true;
	int applied_quality = -1;

	WorldEnvironment *world_environment = nullptr;
	DirectionalLight3D *sun = nullptr;
	Color sky_horizon_color = Color(0.62f, 0.68f, 0.76f);
	Color sky_overcast_color = Color(0.46f, 0.48f, 0.52f);
	float sky_energy = 1.0f;

	LocalVector<Ref<WeatherEffect>> running_effects;

	// Scene values the weather drives, as they were before: object -> property -> value.
	HashMap<ObjectID, HashMap<StringName, Variant>> bases;
	// The values last written to them.
	HashMap<ObjectID, HashMap<StringName, Variant>> written;

	void _find_scene_nodes();
	void _read_sky_colors();
	void _sync_effects();
	void _stop_effects();
	void _process_weather(double p_delta);
	bool _update_editor_preview();
	void _set_target(const Ref<WeatherProfile> &p_profile, float p_duration);
	Ref<WeatherProfile> _match_class(const Ref<WeatherProfile> &p_profile, const Ref<WeatherProfile> &p_into) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;

public:
	static void register_project_settings();
	static void register_shader_globals();
	static void register_builtin_resources();
	static void free_builtin_resources();
	static TypedArray<WeatherEffect> create_default_effects();

	void set_library(const Ref<WeatherLibrary> &p_library);
	Ref<WeatherLibrary> get_library() const;
	Ref<WeatherLibrary> get_effective_library() const;
	void set_weather(const String &p_name);
	String get_weather() const;
	void set_custom_profile(const Ref<WeatherProfile> &p_profile);
	Ref<WeatherProfile> get_custom_profile() const;
	void set_effects(const TypedArray<WeatherEffect> &p_effects);
	TypedArray<WeatherEffect> get_effects() const;
	Ref<WeatherEffect> get_effect(const StringName &p_class) const;
	void set_transition_time(float p_time);
	float get_transition_time() const;
	void set_gust_direction_swing(float p_degrees);
	float get_gust_direction_swing() const;
	void set_gust_rain_bursts(float p_amount);
	float get_gust_rain_bursts() const;
	void set_quality(Quality p_quality);
	Quality get_quality() const;
	void set_wetting_time(float p_time);
	float get_wetting_time() const;
	void set_drying_time(float p_time);
	float get_drying_time() const;
	void set_world_environment_path(const NodePath &p_path);
	NodePath get_world_environment_path() const;
	void set_sun_path(const NodePath &p_path);
	NodePath get_sun_path() const;
	void set_preview_in_editor(bool p_enabled);
	bool is_preview_in_editor() const;

	// Transitions to a weather type of the library, or "Custom".
	// p_duration < 0 uses transition_time; 0 switches at once.
	void change_weather(const String &p_name, float p_duration = -1.0f);
	// Transitions to any profile. One not in the library becomes the custom profile.
	void change_profile(const Ref<WeatherProfile> &p_profile, float p_duration = -1.0f);
	void next_weather();
	Ref<WeatherProfile> get_profile(const String &p_name);
	PackedStringArray get_weather_names() const;
	String get_weather_name() const;

	// The blended state being shown. Read it, don't write it.
	Ref<WeatherProfile> get_current() const;
	Ref<WeatherProfile> get_target() const;
	float get_wetness() const;
	void set_wetness(float p_wetness);
	float get_rain_amount() const;
	float get_gust() const;
	Vector3 get_wind() const;
	Color get_horizon_light() const;
	// Effects that know the sky better (the lighting effect) set it.
	void set_horizon_light(const Color &p_color);
	Color get_sky_horizon_color() const;
	Color get_sky_overcast_color() const;
	float get_sky_energy() const;
	// The sky effect reports its colours (sRGB) and brightness.
	void set_sky_colors(const Color &p_horizon, const Color &p_overcast, float p_energy);
	float get_sun_through_clouds() const;
	double get_time() const;
	Quality get_effective_quality() const;
	bool is_previewing() const;

	Camera3D *get_view_camera() const;
	WorldEnvironment *get_world_environment() const;
	Ref<Environment> get_environment() const;
	Ref<CameraAttributes> get_camera_attributes() const;
	DirectionalLight3D *get_sun() const;

	// Scene values: drive() sets a value and remembers the scene's own one;
	// get_base() returns that (an outside change since the last drive()
	// becomes the new base); restore_scene() puts them all back.
	void drive(Object *p_object, const StringName &p_property, const Variant &p_value);
	Variant get_base(Object *p_object, const StringName &p_property);
	// Puts one driven value back and stops tracking it (an effect that stops).
	void release(Object *p_object, const StringName &p_property);
	void restore_scene();

	Weather();
	~Weather();
};

VARIANT_ENUM_CAST(Weather::Quality);
