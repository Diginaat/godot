/**************************************************************************/
/*  wind_server.h                                                         */
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

#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "scene/resources/shader_include.h"

// The one global wind of the world. Everything that reacts to wind reads it
// here: foliage shaders through global shader parameters (see
// get_shader_include_path()), rigid bodies through WindReceiver3D, cloth and
// scripts through sample(). The weather sets it; without a weather node it
// keeps the project defaults (weather/wind/*).
//
// The wind at a point is the mean wind plus gusts: a noise pattern that moves
// with the wind, so gust fronts sweep across grass fields at wind speed. The
// same noise runs on the CPU (sample()) and in shaders (wind_sample()).
class WindServer : public Object {
	GDCLASS(WindServer, Object);

public:
	// The gust noise tiles every this many lattice cells.
	static constexpr int NOISE_PERIOD = 256;
	// The gust offset wraps at this many gust_scale lengths, a whole period of
	// every noise octave (scales 0.5, 1 and 2), so the wrap is invisible.
	static constexpr float OFFSET_WRAP = 512.0f;

	// What sample() and the shaders read. Copied out under the lock.
	struct State {
		Vector2 direction = Vector2(0.0f, -1.0f); // Horizontal (x, z), unit length.
		float speed = 3.0f;
		float gustiness = 0.3f;
		float inv_gust_scale = 1.0f / 40.0f;
		float direction_swing = 0.0f; // Radians at gustiness 1.
		float turbulence = 0.3f;
		Vector2 offset; // Gust pattern position, moves with the wind.
		float drift = 0.0f; // Slow change of the pattern itself.
	};

private:
	static WindServer *singleton;

	BinaryMutex lock;
	State state;

	float direction_degrees = 0.0f;
	float gust_scale = 40.0f;
	float direction_swing_degrees = 20.0f;
	float air_density = 1.225f;
	float time_scale = 1.0f;
	bool auto_update = true;

	uint64_t last_ticks = 0;
	bool globals_registered = false;
	Ref<ShaderInclude> shader_include;

	void _frame_pre_draw();
	void _update_direction();
	void _push_shader_globals(const State &p_state);

protected:
	static void _bind_methods();

public:
	static WindServer *get_singleton() { return singleton; }
	static void register_project_settings();

	// Global shader parameter names (all vec4).
	static const char *GLOBAL_MEAN; // dir.x, dir.z, speed, gustiness
	static const char *GLOBAL_GUST; // offset.x, offset.z, 1 / gust scale, direction swing
	static const char *GLOBAL_MOTION; // drift, turbulence, 0, 0

	// Gust noise, identical to wind_noise() in the shader include.
	static float noise(const Vector2 &p_position);
	static Vector3 sample_state(const State &p_state, const Vector3 &p_position);

	void initialize();
	void finalize();

	State get_state() const;
	Vector3 sample(const Vector3 &p_position) const;
	Vector3 get_mean_wind() const;
	float get_gust_factor(const Vector3 &p_position) const;
	void advance(double p_delta);

	void set_direction_degrees(float p_degrees);
	float get_direction_degrees() const;
	Vector3 get_direction() const;
	void set_speed(float p_speed);
	float get_speed() const;
	void set_gustiness(float p_gustiness);
	float get_gustiness() const;
	void set_gust_scale(float p_scale);
	float get_gust_scale() const;
	void set_direction_swing_degrees(float p_degrees);
	float get_direction_swing_degrees() const;
	void set_turbulence(float p_turbulence);
	float get_turbulence() const;
	void set_air_density(float p_density);
	float get_air_density() const;
	void set_time_scale(float p_scale);
	float get_time_scale() const;
	void set_auto_update(bool p_enabled);
	bool is_auto_update() const;

	String get_shader_include_path() const;
	String get_shader_include_code() const;

	WindServer();
	~WindServer();
};
