/**************************************************************************/
/*  wind_server.cpp                                                       */
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

#include "wind_server.h"

#include "wind_shader_include.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/resource.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/io/image.h"
#include "core/os/os.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/shader_include.h"
#include "servers/rendering/rendering_server.h"

WindServer *WindServer::singleton = nullptr;

const char *WindServer::GLOBAL_MEAN = "wind_mean";
const char *WindServer::GLOBAL_GUST = "wind_gust";
const char *WindServer::GLOBAL_MOTION = "wind_motion";
const char *WindServer::GLOBAL_ZONES = "wind_zones";

static const char *WIND_INCLUDE_PATH = "res://.builtin/wind.gdshaderinc";

// GLSL mod(): the result has the sign of the divisor.
static _FORCE_INLINE_ float _glsl_mod(float p_x, float p_y) {
	return p_x - p_y * Math::floor(p_x / p_y);
}

static _FORCE_INLINE_ float _fract(float p_x) {
	return p_x - Math::floor(p_x);
}

static float _hash(float p_x, float p_y) {
	p_x = _glsl_mod(p_x, (float)WindServer::NOISE_PERIOD);
	p_y = _glsl_mod(p_y, (float)WindServer::NOISE_PERIOD);
	float a = _fract(p_x * 0.1031f);
	float b = _fract(p_y * 0.1031f);
	float c = a;
	const float d = a * (b + 33.33f) + b * (c + 33.33f) + c * (a + 33.33f);
	a += d;
	b += d;
	c += d;
	return _fract((a + b) * c);
}

float WindServer::noise(const Vector2 &p_position) {
	const float ix = Math::floor(p_position.x);
	const float iy = Math::floor(p_position.y);
	const float fx = p_position.x - ix;
	const float fy = p_position.y - iy;
	const float ux = fx * fx * (3.0f - 2.0f * fx);
	const float uy = fy * fy * (3.0f - 2.0f * fy);
	const float a = _hash(ix, iy);
	const float b = _hash(ix + 1.0f, iy);
	const float c = _hash(ix, iy + 1.0f);
	const float d = _hash(ix + 1.0f, iy + 1.0f);
	return Math::lerp(Math::lerp(a, b, ux), Math::lerp(c, d, ux), uy);
}

float WindServer::gust_state(const State &p_state, const Vector3 &p_position) {
	const Vector2 q = (Vector2(p_position.x, p_position.z) - p_state.offset) * p_state.inv_gust_scale;
	const float g = noise(q) * 0.65f + noise(q * 2.0f + Vector2(17.0f + p_state.drift, 5.0f + p_state.drift)) * 0.35f;
	return CLAMP((g - 0.5f) * 2.2f + 0.5f, 0.0f, 1.0f);
}

Vector3 WindServer::sample_state(const State &p_state, const Vector3 &p_position) {
	const Vector2 q = (Vector2(p_position.x, p_position.z) - p_state.offset) * p_state.inv_gust_scale;
	const float gust = gust_state(p_state, p_position);
	const float speed = p_state.speed * (1.0f + p_state.gustiness * (gust * 1.4f - 0.4f));
	const float swing = (noise(q * 0.5f + Vector2(41.0f, 73.0f)) - 0.5f) * 2.0f * p_state.direction_swing * p_state.gustiness;
	const float c = Math::cos(swing);
	const float s = Math::sin(swing);
	const Vector2 d(p_state.direction.x * c - p_state.direction.y * s, p_state.direction.x * s + p_state.direction.y * c);
	return Vector3(d.x, 0.0f, d.y) * MAX(speed, 0.0f);
}

Vector3 WindServer::apply_zone(const Zone &p_zone, const Vector3 &p_position, const Vector3 &p_wind) {
	const Vector3 offset = p_position - p_zone.origin;
	const Vector3 unit = p_zone.to_unit.xform(offset);
	const float distance = p_zone.shape == ZONE_SPHERE ? unit.length() : MAX(MAX(Math::abs(unit.x), Math::abs(unit.y)), Math::abs(unit.z));
	if (distance >= 1.0f) {
		return p_wind;
	}
	// Full strength inside, fading out over the falloff toward the edge
	// (GLSL smoothstep).
	const float falloff = MAX(p_zone.falloff, 0.001f);
	const float t = CLAMP((distance - (1.0f - falloff)) / falloff, 0.0f, 1.0f);
	const float weight = 1.0f - t * t * (3.0f - 2.0f * t);
	Vector3 zone_wind;
	switch (p_zone.mode) {
		case ZONE_DIRECTIONAL: {
			zone_wind = p_zone.axis * p_zone.strength;
		} break;
		case ZONE_RADIAL: {
			const float length = offset.length();
			zone_wind = length > 0.001f ? offset * (p_zone.strength / length) : Vector3();
		} break;
		case ZONE_VORTEX: {
			const Vector3 around = p_zone.axis.cross(offset);
			const float length = around.length();
			zone_wind = length > 0.001f ? around * (p_zone.strength / length) : Vector3();
		} break;
	}
	return p_zone.replace ? p_wind.lerp(zone_wind, weight) : p_wind + zone_wind * weight;
}

void WindServer::register_project_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "weather/wind/default_speed", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:m/s"), 3.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "weather/wind/default_direction_degrees", PROPERTY_HINT_RANGE, "0,360,0.1,degrees"), 30.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "weather/wind/default_gustiness", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.3);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "weather/wind/gust_scale", PROPERTY_HINT_RANGE, "1,500,0.1,suffix:m"), 40.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "weather/wind/default_turbulence", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.3);
}

void WindServer::initialize() {
	direction_degrees = GLOBAL_GET("weather/wind/default_direction_degrees");
	gust_scale = MAX(1.0f, (float)GLOBAL_GET("weather/wind/gust_scale"));
	{
		MutexLock guard(lock);
		state.speed = GLOBAL_GET("weather/wind/default_speed");
		state.gustiness = GLOBAL_GET("weather/wind/default_gustiness");
		state.turbulence = GLOBAL_GET("weather/wind/default_turbulence");
		state.inv_gust_scale = 1.0f / gust_scale;
		state.direction_swing = Math::deg_to_rad(direction_swing_degrees);
	}
	_update_direction();

	shader_include.instantiate();
	shader_include->set_code(wind_shader_include_code);
	// Served from the resource cache: #include finds it without a file.
	shader_include->set_path(WIND_INCLUDE_PATH, true);

	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		// Registered before any scene loads, so shaders using them compile.
		// A project that declares the same globals (vec4) just sets them later.
		rs->global_shader_parameter_add(GLOBAL_MEAN, RSE::GLOBAL_VAR_TYPE_VEC4, Vector4());
		rs->global_shader_parameter_add(GLOBAL_GUST, RSE::GLOBAL_VAR_TYPE_VEC4, Vector4());
		rs->global_shader_parameter_add(GLOBAL_MOTION, RSE::GLOBAL_VAR_TYPE_VEC4, Vector4());
		rs->global_shader_parameter_add(GLOBAL_ZONES, RSE::GLOBAL_VAR_TYPE_SAMPLER2D, Variant());
		globals_registered = true;
		_update_zone_texture();
		_push_shader_globals(get_state());
		rs->connect(SNAME("frame_pre_draw"), callable_mp(this, &WindServer::_frame_pre_draw));
	}
	last_ticks = OS::get_singleton()->get_ticks_usec();
}

void WindServer::finalize() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && rs->is_connected(SNAME("frame_pre_draw"), callable_mp(this, &WindServer::_frame_pre_draw))) {
		rs->disconnect(SNAME("frame_pre_draw"), callable_mp(this, &WindServer::_frame_pre_draw));
	}
	shader_include.unref();
	zone_texture.unref();
}

void WindServer::_frame_pre_draw() {
	const uint64_t ticks = OS::get_singleton()->get_ticks_usec();
	// Clamped: a long stall (loading, a breakpoint) must not jump the gusts.
	const double delta = MIN((ticks - last_ticks) / 1000000.0, 0.1);
	last_ticks = ticks;
	if (auto_update) {
		advance(delta * Engine::get_singleton()->get_time_scale());
	}
	bool update_zones = false;
	{
		MutexLock guard(lock);
		update_zones = zones_dirty;
	}
	if (update_zones) {
		_update_zone_texture();
		_push_shader_globals(get_state());
	}
}

void WindServer::_update_zone_texture() {
	// One row of float texels, ZONE_TEXELS per zone:
	//  0: origin, flags (mode + 4 * shape + 8 * replace)
	//  1-3: rows of the world-to-unit basis; w: falloff, strength, 0
	//  4: axis
	const int width = MAX_ZONES * ZONE_TEXELS;
	Vector<uint8_t> data;
	data.resize(width * 4 * sizeof(float));
	data.fill(0);
	float *texels = (float *)data.ptrw();
	{
		MutexLock guard(lock);
		zones_dirty = false;
		for (uint32_t i = 0; i < zones.size(); i++) {
			const Zone &zone = zones[i].zone;
			float *t = texels + i * ZONE_TEXELS * 4;
			const float values[ZONE_TEXELS * 4] = {
				zone.origin.x, zone.origin.y, zone.origin.z, float(zone.mode + 4 * zone.shape + 8 * (zone.replace ? 1 : 0)),
				zone.to_unit.rows[0].x, zone.to_unit.rows[0].y, zone.to_unit.rows[0].z, MAX(zone.falloff, 0.001f),
				zone.to_unit.rows[1].x, zone.to_unit.rows[1].y, zone.to_unit.rows[1].z, zone.strength,
				zone.to_unit.rows[2].x, zone.to_unit.rows[2].y, zone.to_unit.rows[2].z, 0.0f,
				zone.axis.x, zone.axis.y, zone.axis.z, 0.0f
			};
			memcpy(t, values, sizeof(values));
		}
	}
	const Ref<Image> image = Image::create_from_data(width, 1, false, Image::FORMAT_RGBAF, data);
	if (zone_texture.is_valid()) {
		zone_texture->update(image);
	} else {
		zone_texture = ImageTexture::create_from_image(image);
		RenderingServer *rs = RenderingServer::get_singleton();
		if (globals_registered && rs) {
			rs->global_shader_parameter_set(GLOBAL_ZONES, zone_texture);
		}
	}
}

void WindServer::set_zone(const void *p_owner, const Zone &p_zone) {
	MutexLock guard(lock);
	for (ZoneEntry &entry : zones) {
		if (entry.owner == p_owner) {
			entry.zone = p_zone;
			zones_dirty = true;
			return;
		}
	}
	ERR_FAIL_COND_MSG(zones.size() >= (uint32_t)MAX_ZONES, vformat("Too many wind zones: at most %d at once.", MAX_ZONES));
	ZoneEntry entry;
	entry.owner = p_owner;
	entry.zone = p_zone;
	zones.push_back(entry);
	zones_dirty = true;
}

void WindServer::remove_zone(const void *p_owner) {
	MutexLock guard(lock);
	for (uint32_t i = 0; i < zones.size(); i++) {
		if (zones[i].owner == p_owner) {
			zones.remove_at(i);
			zones_dirty = true;
			return;
		}
	}
}

int WindServer::get_zone_count() const {
	MutexLock guard(lock);
	return zones.size();
}

void WindServer::advance(double p_delta) {
	State copy;
	{
		MutexLock guard(lock);
		const float step = (float)p_delta * time_scale;
		// The gust pattern travels with the mean wind.
		state.offset += state.direction * (state.speed * step);
		const float wrap = OFFSET_WRAP / state.inv_gust_scale;
		state.offset.x = _glsl_mod(state.offset.x, wrap);
		state.offset.y = _glsl_mod(state.offset.y, wrap);
		// The pattern also changes slowly on its own, so calm air isn't frozen.
		state.drift = _glsl_mod(state.drift + step * 0.07f, (float)NOISE_PERIOD);
		copy = state;
	}
	_push_shader_globals(copy);
}

void WindServer::_push_shader_globals(const State &p_state) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!globals_registered || !rs) {
		return;
	}
	rs->global_shader_parameter_set(GLOBAL_MEAN, Vector4(p_state.direction.x, p_state.direction.y, p_state.speed, p_state.gustiness));
	rs->global_shader_parameter_set(GLOBAL_GUST, Vector4(p_state.offset.x, p_state.offset.y, p_state.inv_gust_scale, p_state.direction_swing));
	int zone_count = 0;
	{
		MutexLock guard(lock);
		// The texture holds the zones from the last update, so count those.
		zone_count = zones_dirty ? -1 : (int)zones.size();
	}
	if (zone_count >= 0) {
		shader_zone_count = zone_count;
	}
	rs->global_shader_parameter_set(GLOBAL_MOTION, Vector4(p_state.drift, p_state.turbulence, (float)shader_zone_count, 0.0f));
}

void WindServer::_update_direction() {
	const float heading = Math::deg_to_rad(direction_degrees);
	MutexLock guard(lock);
	// 0 degrees blows toward -Z, clockwise seen from above.
	state.direction = Vector2(Math::sin(heading), -Math::cos(heading));
}

WindServer::State WindServer::get_state() const {
	MutexLock guard(lock);
	return state;
}

Vector3 WindServer::sample(const Vector3 &p_position) const {
	MutexLock guard(lock);
	Vector3 wind = sample_state(state, p_position);
	for (const ZoneEntry &entry : zones) {
		wind = apply_zone(entry.zone, p_position, wind);
	}
	return wind;
}

Vector3 WindServer::sample_global(const Vector3 &p_position) const {
	return sample_state(get_state(), p_position);
}

Vector3 WindServer::get_mean_wind() const {
	const State s = get_state();
	return Vector3(s.direction.x, 0.0f, s.direction.y) * s.speed;
}

float WindServer::get_gust(const Vector3 &p_position) const {
	return gust_state(get_state(), p_position);
}

float WindServer::get_gust_factor(const Vector3 &p_position) const {
	const State s = get_state();
	if (s.speed <= 0.0f) {
		return 1.0f;
	}
	return sample_state(s, p_position).length() / s.speed;
}

void WindServer::set_direction_degrees(float p_degrees) {
	direction_degrees = Math::fposmod(p_degrees, 360.0f);
	_update_direction();
}

float WindServer::get_direction_degrees() const {
	return direction_degrees;
}

Vector3 WindServer::get_direction() const {
	const State s = get_state();
	return Vector3(s.direction.x, 0.0f, s.direction.y);
}

void WindServer::set_speed(float p_speed) {
	MutexLock guard(lock);
	state.speed = MAX(0.0f, p_speed);
}

float WindServer::get_speed() const {
	MutexLock guard(lock);
	return state.speed;
}

void WindServer::set_gustiness(float p_gustiness) {
	MutexLock guard(lock);
	state.gustiness = CLAMP(p_gustiness, 0.0f, 1.0f);
}

float WindServer::get_gustiness() const {
	MutexLock guard(lock);
	return state.gustiness;
}

void WindServer::set_gust_scale(float p_scale) {
	gust_scale = MAX(1.0f, p_scale);
	MutexLock guard(lock);
	state.inv_gust_scale = 1.0f / gust_scale;
}

float WindServer::get_gust_scale() const {
	return gust_scale;
}

void WindServer::set_direction_swing_degrees(float p_degrees) {
	direction_swing_degrees = CLAMP(p_degrees, 0.0f, 90.0f);
	MutexLock guard(lock);
	state.direction_swing = Math::deg_to_rad(direction_swing_degrees);
}

float WindServer::get_direction_swing_degrees() const {
	return direction_swing_degrees;
}

void WindServer::set_turbulence(float p_turbulence) {
	MutexLock guard(lock);
	state.turbulence = CLAMP(p_turbulence, 0.0f, 1.0f);
}

float WindServer::get_turbulence() const {
	MutexLock guard(lock);
	return state.turbulence;
}

void WindServer::set_air_density(float p_density) {
	air_density = MAX(0.0f, p_density);
}

float WindServer::get_air_density() const {
	return air_density;
}

void WindServer::set_time_scale(float p_scale) {
	MutexLock guard(lock);
	time_scale = MAX(0.0f, p_scale);
}

float WindServer::get_time_scale() const {
	return time_scale;
}

void WindServer::set_auto_update(bool p_enabled) {
	auto_update = p_enabled;
}

bool WindServer::is_auto_update() const {
	return auto_update;
}

String WindServer::get_shader_include_path() const {
	return WIND_INCLUDE_PATH;
}

String WindServer::get_shader_include_code() const {
	return wind_shader_include_code;
}

void WindServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("sample", "position"), &WindServer::sample);
	ClassDB::bind_method(D_METHOD("sample_global", "position"), &WindServer::sample_global);
	ClassDB::bind_method(D_METHOD("get_zone_count"), &WindServer::get_zone_count);
	ClassDB::bind_method(D_METHOD("get_mean_wind"), &WindServer::get_mean_wind);
	ClassDB::bind_method(D_METHOD("get_gust_factor", "position"), &WindServer::get_gust_factor);
	ClassDB::bind_method(D_METHOD("get_gust", "position"), &WindServer::get_gust);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &WindServer::advance);
	ClassDB::bind_method(D_METHOD("get_direction"), &WindServer::get_direction);
	ClassDB::bind_method(D_METHOD("get_shader_include_path"), &WindServer::get_shader_include_path);
	ClassDB::bind_method(D_METHOD("get_shader_include_code"), &WindServer::get_shader_include_code);

	ClassDB::bind_method(D_METHOD("set_direction_degrees", "degrees"), &WindServer::set_direction_degrees);
	ClassDB::bind_method(D_METHOD("get_direction_degrees"), &WindServer::get_direction_degrees);
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &WindServer::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &WindServer::get_speed);
	ClassDB::bind_method(D_METHOD("set_gustiness", "gustiness"), &WindServer::set_gustiness);
	ClassDB::bind_method(D_METHOD("get_gustiness"), &WindServer::get_gustiness);
	ClassDB::bind_method(D_METHOD("set_gust_scale", "scale"), &WindServer::set_gust_scale);
	ClassDB::bind_method(D_METHOD("get_gust_scale"), &WindServer::get_gust_scale);
	ClassDB::bind_method(D_METHOD("set_direction_swing_degrees", "degrees"), &WindServer::set_direction_swing_degrees);
	ClassDB::bind_method(D_METHOD("get_direction_swing_degrees"), &WindServer::get_direction_swing_degrees);
	ClassDB::bind_method(D_METHOD("set_turbulence", "turbulence"), &WindServer::set_turbulence);
	ClassDB::bind_method(D_METHOD("get_turbulence"), &WindServer::get_turbulence);
	ClassDB::bind_method(D_METHOD("set_air_density", "density"), &WindServer::set_air_density);
	ClassDB::bind_method(D_METHOD("get_air_density"), &WindServer::get_air_density);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &WindServer::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &WindServer::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_auto_update", "enabled"), &WindServer::set_auto_update);
	ClassDB::bind_method(D_METHOD("is_auto_update"), &WindServer::is_auto_update);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "direction_degrees", PROPERTY_HINT_RANGE, "0,360,0.1,degrees"), "set_direction_degrees", "get_direction_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:m/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gustiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_gustiness", "get_gustiness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_scale", PROPERTY_HINT_RANGE, "1,500,0.1,suffix:m"), "set_gust_scale", "get_gust_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "direction_swing_degrees", PROPERTY_HINT_RANGE, "0,90,0.1,degrees"), "set_direction_swing_degrees", "get_direction_swing_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbulence", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_turbulence", "get_turbulence");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "air_density", PROPERTY_HINT_RANGE, "0,2,0.001,suffix:kg/m³"), "set_air_density", "get_air_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_update"), "set_auto_update", "is_auto_update");
}

WindServer::WindServer() {
	singleton = this;
}

WindServer::~WindServer() {
	singleton = nullptr;
}
