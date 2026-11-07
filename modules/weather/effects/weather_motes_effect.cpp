/**************************************************************************/
/*  weather_motes_effect.cpp                                              */
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

#include "weather_motes_effect.h"

#include "weather_haze_shaders.h"

#include "../weather.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"

void WeatherMotesEffect::start(Weather *p_weather) {
	weather = p_weather;
	_build();
	WeatherEffect::start(p_weather);
}

void WeatherMotesEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	_clear();
	weather = nullptr;
}

void WeatherMotesEffect::apply_quality(Weather *p_weather, int p_quality) {
	static const int motes[] = { 0, 3000, 6000, 12000 };
	const int count = motes[CLAMP(p_quality, (int)Weather::QUALITY_LOW, (int)Weather::QUALITY_ULTRA) - Weather::QUALITY_LOW];
	if (count != max_motes) {
		max_motes = count;
		dirty = true;
	}
	WeatherEffect::apply_quality(p_weather, p_quality);
}

void WeatherMotesEffect::_build() {
	dirty = false;
	intensity = -1.0f;
	if (max_motes <= 0) {
		return;
	}
	Ref<ParticleProcessMaterial> process;
	process.instantiate();
	process->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	process->set_emission_box_extents(Vector3(radius, height * 0.5f, radius));
	process->set_gravity(Vector3());
	// Still air: wander a little in every direction (the wind is added in the shader).
	process->set_direction(Vector3(1.0f, 0.0f, 0.0f));
	process->set_spread(180.0f);
	process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.05f);
	process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.3f);
	// Slow, swirling drift.
	process->set_turbulence_enabled(true);
	process->set_turbulence_noise_strength(0.6f);
	process->set_turbulence_noise_scale(3.0f);
	process->set_param_min(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.03f);
	process->set_param_max(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.12f);

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(weather_mote_shader_code);
	material.instantiate();
	material->set_shader(shader);
	// Faded out before the box edge, where motes wrap around.
	material->set_shader_parameter("far_fade", radius * 0.95f);
	material->set_shader_parameter("box_size", Vector2(radius * 2.0f, radius * 2.0f));
	Ref<QuadMesh> mesh;
	mesh.instantiate();
	mesh->set_material(material);

	particles = memnew(GPUParticles3D);
	particles->set_name("Motes");
	particles->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	particles->set_amount(max_motes);
	particles->set_lifetime(lifetime);
	particles->set_randomness_ratio(0.5f);
	particles->set_use_local_coordinates(false);
	particles->set_process_material(process);
	particles->set_draw_pass_mesh(0, mesh);
	particles->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	particles->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	particles->set_layer_mask(mote_layers);
	// The shader draws them wrapped around the camera, wherever they really are.
	particles->set_visibility_aabb(AABB(Vector3(-radius * 2.0f, -height * 2.0f, -radius * 2.0f), Vector3(radius * 4.0f, height * 4.0f, radius * 4.0f)));
	particles->set_pre_process_time(lifetime);
	weather->add_child(particles, false, Node::INTERNAL_MODE_FRONT);
}

void WeatherMotesEffect::_clear() {
	if (particles) {
		// Out of the tree at once, so a rebuild can take the same name.
		particles->get_parent()->remove_child(particles);
		particles->queue_free();
		particles = nullptr;
	}
	material.unref();
}

void WeatherMotesEffect::update(Weather *p_weather, double p_delta) {
	if (dirty && weather) {
		_clear();
		_build();
	}
	WeatherEffect::update(p_weather, p_delta);
	if (!particles) {
		return;
	}
	const Camera3D *camera = p_weather->get_view_camera();
	if (camera) {
		// A little below the eyes: more motes in view looking ahead and down.
		particles->set_global_position(camera->get_global_position() - Vector3(0.0f, height * 0.2f, 0.0f));
	}

	// Fog makes mist, and rain throws droplets into the air.
	const Ref<WeatherProfile> current = p_weather->get_current();
	const float new_intensity = CLAMP((current->get_param(WeatherProfile::PARAM_FOG_DENSITY) * 25.0f + p_weather->get_rain_amount() * 0.6f) * amount, 0.0f, 1.0f);
	if (new_intensity != intensity) {
		intensity = new_intensity;
		particles->set_amount_ratio(intensity);
		particles->set_emitting(intensity > 0.001f);
	}

	// Far slower than the wind: at full speed they'd streak past and read as
	// noise, not floating mist. Wrapped every box width, as in the shader.
	const Vector3 wind = p_weather->get_wind();
	const float box = radius * 2.0f;
	wind_offset += (Vector2(wind.x, wind.z) * wind_drift).limit_length(max_drift_speed) * (float)p_delta;
	wind_offset = Vector2(Math::fposmod(wind_offset.x, box), Math::fposmod(wind_offset.y, box));
	material->set_shader_parameter(SNAME("wind_offset"), wind_offset);
	const Color light = p_weather->get_horizon_light();
	material->set_shader_parameter(SNAME("sky_light"), Vector3(light.r, light.g, light.b));
}

int WeatherMotesEffect::get_max_motes() const {
	return max_motes;
}

float WeatherMotesEffect::get_intensity() const {
	return MAX(intensity, 0.0f);
}

// Settings.

void WeatherMotesEffect::_changed() {
	// Rebuilt on the next update while running.
	dirty = true;
	emit_changed();
}

void WeatherMotesEffect::set_amount(float p_amount) {
	amount = MAX(0.0f, p_amount);
	intensity = -1.0f;
	emit_changed();
}

float WeatherMotesEffect::get_amount() const {
	return amount;
}

void WeatherMotesEffect::set_radius(float p_radius) {
	radius = MAX(1.0f, p_radius);
	_changed();
}

float WeatherMotesEffect::get_radius() const {
	return radius;
}

void WeatherMotesEffect::set_height(float p_height) {
	height = MAX(0.5f, p_height);
	_changed();
}

float WeatherMotesEffect::get_height() const {
	return height;
}

void WeatherMotesEffect::set_lifetime(float p_seconds) {
	lifetime = MAX(0.1f, p_seconds);
	_changed();
}

float WeatherMotesEffect::get_lifetime() const {
	return lifetime;
}

void WeatherMotesEffect::set_wind_drift(float p_drift) {
	wind_drift = MAX(0.0f, p_drift);
	emit_changed();
}

float WeatherMotesEffect::get_wind_drift() const {
	return wind_drift;
}

void WeatherMotesEffect::set_max_drift_speed(float p_speed) {
	max_drift_speed = MAX(0.0f, p_speed);
	emit_changed();
}

float WeatherMotesEffect::get_max_drift_speed() const {
	return max_drift_speed;
}

void WeatherMotesEffect::set_mote_layers(uint32_t p_layers) {
	mote_layers = p_layers;
	_changed();
}

uint32_t WeatherMotesEffect::get_mote_layers() const {
	return mote_layers;
}

void WeatherMotesEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_amount", "amount"), &WeatherMotesEffect::set_amount);
	ClassDB::bind_method(D_METHOD("get_amount"), &WeatherMotesEffect::get_amount);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &WeatherMotesEffect::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &WeatherMotesEffect::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &WeatherMotesEffect::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &WeatherMotesEffect::get_height);
	ClassDB::bind_method(D_METHOD("set_lifetime", "seconds"), &WeatherMotesEffect::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &WeatherMotesEffect::get_lifetime);
	ClassDB::bind_method(D_METHOD("set_wind_drift", "drift"), &WeatherMotesEffect::set_wind_drift);
	ClassDB::bind_method(D_METHOD("get_wind_drift"), &WeatherMotesEffect::get_wind_drift);
	ClassDB::bind_method(D_METHOD("set_max_drift_speed", "speed"), &WeatherMotesEffect::set_max_drift_speed);
	ClassDB::bind_method(D_METHOD("get_max_drift_speed"), &WeatherMotesEffect::get_max_drift_speed);
	ClassDB::bind_method(D_METHOD("set_mote_layers", "layers"), &WeatherMotesEffect::set_mote_layers);
	ClassDB::bind_method(D_METHOD("get_mote_layers"), &WeatherMotesEffect::get_mote_layers);
	ClassDB::bind_method(D_METHOD("get_max_motes"), &WeatherMotesEffect::get_max_motes);
	ClassDB::bind_method(D_METHOD("get_intensity"), &WeatherMotesEffect::get_intensity);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amount", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_amount", "get_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "1,50,0.1,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.5,20,0.1,or_greater,suffix:m"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0.1,20,0.1,or_greater,suffix:s"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_drift", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_wind_drift", "get_wind_drift");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_drift_speed", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:m/s"), "set_max_drift_speed", "get_max_drift_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mote_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_mote_layers", "get_mote_layers");
}
