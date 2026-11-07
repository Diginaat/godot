/**************************************************************************/
/*  weather_haze_effect.cpp                                               */
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

#include "weather_haze_effect.h"

#include "weather_haze_shaders.h"

#include "../weather.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/fog_volume.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

// World-snapped steps for the volume, so it doesn't move every frame.
static const Vector3 haze_snap = Vector3(8.0f, 4.0f, 8.0f);

void WeatherHazeEffect::start(Weather *p_weather) {
	weather = p_weather;
	if (quality_enabled) {
		_build();
	}
	WeatherEffect::start(p_weather);
}

void WeatherHazeEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	_clear();
	weather = nullptr;
}

void WeatherHazeEffect::apply_quality(Weather *p_weather, int p_quality) {
	// Low quality has no haze: it needs volumetric fog, which is expensive.
	quality_enabled = p_quality >= Weather::QUALITY_MEDIUM;
	const int resolution = p_quality == Weather::QUALITY_MEDIUM ? 48 : 64;
	if (resolution != noise_resolution) {
		noise_resolution = resolution;
		_update_noise_resolution();
	}
	if (weather) {
		if (quality_enabled && !volume) {
			_build();
		} else if (!quality_enabled && volume) {
			_clear();
		}
	}
	WeatherEffect::apply_quality(p_weather, p_quality);
}

void WeatherHazeEffect::_build() {
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_fractal_octaves(3);
	noise->set_seed((int)(Math::rand() & 0x7fffffff));
	noise_texture.instantiate();
	noise_texture->set_seamless(true);
	noise_texture->set_noise(noise);
	_update_noise_resolution();

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(weather_haze_shader_code);
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("noise_texture", noise_texture);
	material->set_shader_parameter("patch_size", patch_size);

	volume = memnew(FogVolume);
	volume->set_name("Haze");
	volume->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	volume->set_shape(RSE::FOG_VOLUME_SHAPE_BOX);
	volume->set_size(_volume_size());
	volume->set_material(material);
	volume->set_visible(false);
	weather->add_child(volume, false, Node::INTERNAL_MODE_FRONT);
	ground = Math::NaN;
	ground_target = Math::NaN;
}

void WeatherHazeEffect::_clear() {
	if (volume) {
		volume->get_parent()->remove_child(volume);
		volume->queue_free();
		volume = nullptr;
	}
	material.unref();
	noise_texture.unref();
	_release_environment();
}

void WeatherHazeEffect::_release_environment() {
	if (!driving || !weather) {
		return;
	}
	driving = false;
	const Ref<Environment> environment = weather->get_environment();
	if (environment.is_null()) {
		return;
	}
	// The lighting effect drives some of these too; it takes them up again
	// from the scene's values on its next update.
	for (const char *property : { "volumetric_fog_enabled", "volumetric_fog_density", "volumetric_fog_length", "volumetric_fog_sky_affect", "volumetric_fog_ambient_inject" }) {
		weather->release(environment.ptr(), StringName(property));
	}
}

void WeatherHazeEffect::_update_noise_resolution() {
	if (noise_texture.is_null()) {
		return;
	}
	// Scaled per texel, so the patches stay the same size at any resolution.
	Ref<FastNoiseLite> noise = noise_texture->get_noise();
	noise->set_frequency(NOISE_FREQUENCY * 64.0f / noise_resolution);
	noise_texture->set_width(noise_resolution);
	noise_texture->set_height(noise_resolution);
	noise_texture->set_depth(noise_resolution);
}

Vector3 WeatherHazeEffect::_volume_size() const {
	// The froxel grid reaches the fog distance ahead, and further out at the
	// corners of a wide view; the margin also holds the soft edges.
	const float width = distance * 4.0f;
	return Vector3(width, DEPTH_BELOW + HEIGHT_ABOVE, width);
}

void WeatherHazeEffect::_update_ground(const Vector3 &p_view_position, const Node *p_camera) {
	PhysicsDirectSpaceState3D *space = nullptr;
	const Ref<World3D> world = weather->get_viewport() ? weather->get_viewport()->find_world_3d() : Ref<World3D>();
	if (world.is_valid()) {
		space = world->get_direct_space_state();
	}
	bool hit = false;
	if (space) {
		PS3DT::RayParameters ray;
		ray.from = p_view_position;
		ray.to = p_view_position + Vector3(0.0f, -GROUND_RAY_LENGTH, 0.0f);
		ray.collision_mask = ground_mask;
		// Skip the body carrying the camera (the player), or the ray hits its feet.
		for (const Node *node = p_camera; node; node = node->get_parent()) {
			if (const CollisionObject3D *body = Object::cast_to<CollisionObject3D>(node)) {
				ray.exclude.insert(body->get_rid());
				break;
			}
		}
		PS3DT::RayResult result;
		hit = space->intersect_ray(ray, result);
		if (hit) {
			ground_target = result.position.y;
		}
	}
	if (!hit && (Math::is_nan(ground_target) || !Engine::get_singleton()->is_editor_hint())) {
		// The free-flying editor camera keeps the last ground it found.
		ground_target = p_view_position.y - camera_height;
	}
}

void WeatherHazeEffect::update(Weather *p_weather, double p_delta) {
	WeatherEffect::update(p_weather, p_delta);
	const Ref<Environment> environment = p_weather->get_environment();
	const Camera3D *camera = p_weather->get_view_camera();
	if (!volume || environment.is_null() || !camera) {
		return;
	}
	Environment *env = environment.ptr();
	const Vector3 view_position = camera->get_global_position();
	_update_ground(view_position, camera);
	if (Math::is_nan(ground)) {
		ground = ground_target;
	} else {
		ground = Math::lerp(ground, ground_target, 1.0f - Math::exp(-(float)p_delta / MAX(ground_smoothing, 0.001f)));
	}

	// All fog goes into the haze, the environment's own fog too: it thins
	// out with height, so it can cover the horizon without hiding the
	// clouds overhead.
	const Ref<WeatherProfile> current = p_weather->get_current();
	const float base_fog = p_weather->get_base(env, SNAME("volumetric_fog_density"));
	const float mist = base_fog + current->get_param(WeatherProfile::PARAM_FOG_DENSITY) * density;
	const float rain = p_weather->get_rain_amount();
	const float rain_haze = rain_density * rain;
	const float spray = spray_density * rain * rain;
	const bool visible = mist + rain_haze + spray > 0.0001f;

	driving = true;
	p_weather->drive(env, SNAME("volumetric_fog_enabled"), (bool)p_weather->get_base(env, SNAME("volumetric_fog_enabled")) || visible);
	p_weather->drive(env, SNAME("volumetric_fog_density"), 0.0f);
	p_weather->drive(env, SNAME("volumetric_fog_length"), distance);
	// Light beams and haze in front of the sky and clouds show too.
	p_weather->drive(env, SNAME("volumetric_fog_sky_affect"), sky_affect);
	// The haze is lit by the sky like real haze: under clouds there's little
	// direct sun, but the whole sky lights it. Emission would tint it green
	// (fog volume emission is stored at very low precision).
	p_weather->drive(env, SNAME("volumetric_fog_ambient_inject"), brightness);

	volume->set_visible(visible);
	if (!visible) {
		return;
	}
	const Vector3 size = _volume_size();
	if (!volume->get_size().is_equal_approx(size)) {
		volume->set_size(size);
	}

	// Wind and fall speed from the rain, so the shafts lean like the drops.
	float fall_speed = 11.0f;
	float wind_multiplier = 0.85f;
	const Ref<WeatherEffect> rain_effect = p_weather->get_effect(SNAME("WeatherRainEffect"));
	if (rain_effect.is_valid()) {
		fall_speed = rain_effect->get(SNAME("fall_speed"));
		wind_multiplier = rain_effect->get(SNAME("wind_multiplier"));
	}
	const Vector3 ground_wind = p_weather->get_wind();
	const Vector3 wind = Vector3(ground_wind.x, 0.0f, ground_wind.z) * wind_multiplier;
	// Wrapped every tile to keep precision (the shader's noise tiles there).
	drift += Vector3(wind.x, shaft_evolve_speed * patch_size, wind.z) * (float)p_delta;
	drift = Vector3(Math::fposmod(drift.x, patch_size), Math::fposmod(drift.y, patch_size), Math::fposmod(drift.z, patch_size));

	material->set_shader_parameter(SNAME("ground_height"), ground);
	material->set_shader_parameter(SNAME("mist_density"), mist);
	material->set_shader_parameter(SNAME("height_falloff"), height_falloff);
	material->set_shader_parameter(SNAME("rain_height"), rain_height);
	material->set_shader_parameter(SNAME("patchiness"), patchiness);
	material->set_shader_parameter(SNAME("wisp_strength"), wisp_strength);
	material->set_shader_parameter(SNAME("rain_density"), rain_haze);
	material->set_shader_parameter(SNAME("rain_slant"), Vector2(wind.x, wind.z) / MAX(fall_speed, 0.1f));
	material->set_shader_parameter(SNAME("spray_density"), spray);
	material->set_shader_parameter(SNAME("patch_size"), patch_size);
	// Neutral droplets: no storm tint, the dim sky already darkens it.
	material->set_shader_parameter(SNAME("albedo"), p_weather->get_base(env, SNAME("volumetric_fog_albedo")));
	// The shader takes the evolution in tiles.
	material->set_shader_parameter(SNAME("drift"), Vector3(drift.x, drift.y / patch_size, drift.z));

	const Vector3 center = Vector3(view_position.x, ground + (HEIGHT_ABOVE - DEPTH_BELOW) * 0.5f, view_position.z);
	volume->set_global_position(center.snapped(haze_snap));
}

float WeatherHazeEffect::get_ground_height() const {
	return ground;
}

bool WeatherHazeEffect::is_active() const {
	return volume != nullptr;
}

// Settings.

void WeatherHazeEffect::set_density(float p_density) {
	density = MAX(0.0f, p_density);
	emit_changed();
}

float WeatherHazeEffect::get_density() const {
	return density;
}

void WeatherHazeEffect::set_brightness(float p_brightness) {
	brightness = MAX(0.0f, p_brightness);
	emit_changed();
}

float WeatherHazeEffect::get_brightness() const {
	return brightness;
}

void WeatherHazeEffect::set_distance(float p_distance) {
	distance = MAX(1.0f, p_distance);
	emit_changed();
}

float WeatherHazeEffect::get_distance() const {
	return distance;
}

void WeatherHazeEffect::set_sky_affect(float p_affect) {
	sky_affect = CLAMP(p_affect, 0.0f, 1.0f);
	emit_changed();
}

float WeatherHazeEffect::get_sky_affect() const {
	return sky_affect;
}

void WeatherHazeEffect::set_rain_density(float p_density) {
	rain_density = MAX(0.0f, p_density);
	emit_changed();
}

float WeatherHazeEffect::get_rain_density() const {
	return rain_density;
}

void WeatherHazeEffect::set_spray_density(float p_density) {
	spray_density = MAX(0.0f, p_density);
	emit_changed();
}

float WeatherHazeEffect::get_spray_density() const {
	return spray_density;
}

void WeatherHazeEffect::set_height_falloff(float p_falloff) {
	height_falloff = MAX(0.0f, p_falloff);
	emit_changed();
}

float WeatherHazeEffect::get_height_falloff() const {
	return height_falloff;
}

void WeatherHazeEffect::set_rain_height(float p_height) {
	rain_height = MAX(0.1f, p_height);
	emit_changed();
}

float WeatherHazeEffect::get_rain_height() const {
	return rain_height;
}

void WeatherHazeEffect::set_patchiness(float p_patchiness) {
	patchiness = CLAMP(p_patchiness, 0.0f, 1.0f);
	emit_changed();
}

float WeatherHazeEffect::get_patchiness() const {
	return patchiness;
}

void WeatherHazeEffect::set_wisp_strength(float p_strength) {
	wisp_strength = CLAMP(p_strength, 0.0f, 1.0f);
	emit_changed();
}

float WeatherHazeEffect::get_wisp_strength() const {
	return wisp_strength;
}

void WeatherHazeEffect::set_patch_size(float p_size) {
	patch_size = MAX(1.0f, p_size);
	emit_changed();
}

float WeatherHazeEffect::get_patch_size() const {
	return patch_size;
}

void WeatherHazeEffect::set_shaft_evolve_speed(float p_speed) {
	shaft_evolve_speed = MAX(0.0f, p_speed);
	emit_changed();
}

float WeatherHazeEffect::get_shaft_evolve_speed() const {
	return shaft_evolve_speed;
}

void WeatherHazeEffect::set_ground_smoothing(float p_seconds) {
	ground_smoothing = MAX(0.0f, p_seconds);
	emit_changed();
}

float WeatherHazeEffect::get_ground_smoothing() const {
	return ground_smoothing;
}

void WeatherHazeEffect::set_camera_height(float p_height) {
	camera_height = p_height;
	emit_changed();
}

float WeatherHazeEffect::get_camera_height() const {
	return camera_height;
}

void WeatherHazeEffect::set_ground_mask(uint32_t p_mask) {
	ground_mask = p_mask;
	emit_changed();
}

uint32_t WeatherHazeEffect::get_ground_mask() const {
	return ground_mask;
}

void WeatherHazeEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_density", "density"), &WeatherHazeEffect::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &WeatherHazeEffect::get_density);
	ClassDB::bind_method(D_METHOD("set_brightness", "brightness"), &WeatherHazeEffect::set_brightness);
	ClassDB::bind_method(D_METHOD("get_brightness"), &WeatherHazeEffect::get_brightness);
	ClassDB::bind_method(D_METHOD("set_distance", "distance"), &WeatherHazeEffect::set_distance);
	ClassDB::bind_method(D_METHOD("get_distance"), &WeatherHazeEffect::get_distance);
	ClassDB::bind_method(D_METHOD("set_sky_affect", "affect"), &WeatherHazeEffect::set_sky_affect);
	ClassDB::bind_method(D_METHOD("get_sky_affect"), &WeatherHazeEffect::get_sky_affect);
	ClassDB::bind_method(D_METHOD("set_rain_density", "density"), &WeatherHazeEffect::set_rain_density);
	ClassDB::bind_method(D_METHOD("get_rain_density"), &WeatherHazeEffect::get_rain_density);
	ClassDB::bind_method(D_METHOD("set_spray_density", "density"), &WeatherHazeEffect::set_spray_density);
	ClassDB::bind_method(D_METHOD("get_spray_density"), &WeatherHazeEffect::get_spray_density);
	ClassDB::bind_method(D_METHOD("set_height_falloff", "falloff"), &WeatherHazeEffect::set_height_falloff);
	ClassDB::bind_method(D_METHOD("get_height_falloff"), &WeatherHazeEffect::get_height_falloff);
	ClassDB::bind_method(D_METHOD("set_rain_height", "height"), &WeatherHazeEffect::set_rain_height);
	ClassDB::bind_method(D_METHOD("get_rain_height"), &WeatherHazeEffect::get_rain_height);
	ClassDB::bind_method(D_METHOD("set_patchiness", "patchiness"), &WeatherHazeEffect::set_patchiness);
	ClassDB::bind_method(D_METHOD("get_patchiness"), &WeatherHazeEffect::get_patchiness);
	ClassDB::bind_method(D_METHOD("set_wisp_strength", "strength"), &WeatherHazeEffect::set_wisp_strength);
	ClassDB::bind_method(D_METHOD("get_wisp_strength"), &WeatherHazeEffect::get_wisp_strength);
	ClassDB::bind_method(D_METHOD("set_patch_size", "size"), &WeatherHazeEffect::set_patch_size);
	ClassDB::bind_method(D_METHOD("get_patch_size"), &WeatherHazeEffect::get_patch_size);
	ClassDB::bind_method(D_METHOD("set_shaft_evolve_speed", "speed"), &WeatherHazeEffect::set_shaft_evolve_speed);
	ClassDB::bind_method(D_METHOD("get_shaft_evolve_speed"), &WeatherHazeEffect::get_shaft_evolve_speed);
	ClassDB::bind_method(D_METHOD("set_ground_smoothing", "seconds"), &WeatherHazeEffect::set_ground_smoothing);
	ClassDB::bind_method(D_METHOD("get_ground_smoothing"), &WeatherHazeEffect::get_ground_smoothing);
	ClassDB::bind_method(D_METHOD("set_camera_height", "height"), &WeatherHazeEffect::set_camera_height);
	ClassDB::bind_method(D_METHOD("get_camera_height"), &WeatherHazeEffect::get_camera_height);
	ClassDB::bind_method(D_METHOD("set_ground_mask", "mask"), &WeatherHazeEffect::set_ground_mask);
	ClassDB::bind_method(D_METHOD("get_ground_mask"), &WeatherHazeEffect::get_ground_mask);
	ClassDB::bind_method(D_METHOD("get_ground_height"), &WeatherHazeEffect::get_ground_height);
	ClassDB::bind_method(D_METHOD("is_active"), &WeatherHazeEffect::is_active);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brightness", PROPERTY_HINT_RANGE, "0,3,0.01"), "set_brightness", "get_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance", PROPERTY_HINT_RANGE, "32,512,0.1,or_greater,suffix:m"), "set_distance", "get_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_affect", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_sky_affect", "get_sky_affect");
	ADD_GROUP("Rain", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain_density", PROPERTY_HINT_RANGE, "0,0.2,0.001"), "set_rain_density", "get_rain_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spray_density", PROPERTY_HINT_RANGE, "0,0.5,0.001"), "set_spray_density", "get_spray_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain_height", PROPERTY_HINT_RANGE, "1,100,0.1,or_greater,suffix:m"), "set_rain_height", "get_rain_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_evolve_speed", PROPERTY_HINT_RANGE, "0,0.1,0.001"), "set_shaft_evolve_speed", "get_shaft_evolve_speed");
	ADD_GROUP("Shape", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_falloff", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_height_falloff", "get_height_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "patchiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_patchiness", "get_patchiness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wisp_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_wisp_strength", "get_wisp_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "patch_size", PROPERTY_HINT_RANGE, "8,512,0.1,or_greater,suffix:m"), "set_patch_size", "get_patch_size");
	ADD_GROUP("Ground", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_smoothing", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:s"), "set_ground_smoothing", "get_ground_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_height", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:m"), "set_camera_height", "get_camera_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ground_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_ground_mask", "get_ground_mask");
}
