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
#include "core/math/basis.h"
#include "core/math/vector3.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "scene/resources/shader_include.h"

class ImageTexture;

// The one global wind of the world. Everything that reacts to wind reads it
// here: foliage shaders through global shader parameters (see
// get_shader_include_path()), rigid bodies through WindReceiver3D, cloth and
// scripts through sample(). The weather sets it; without a weather node it
// keeps the project defaults (weather/wind/*).
//
// The wind at a point is the mean wind plus gusts: a noise pattern that moves
// with the wind, so gust fronts sweep across grass fields at wind speed. The
// same noise runs on the CPU (sample()) and in shaders (wind_sample()).
//
// Local wind zones (WindZone3D) add to that wind or replace it inside their
// shape: fans, downwash, blasts, whirlwinds, sheltered spots. The server
// keeps them in a small float texture for the shaders (the global sampler
// wind_zones, ZONE_TEXELS texels per zone) and applies the same math in
// sample().
class WindServer : public Object {
	GDCLASS(WindServer, Object);

public:
	// The gust noise tiles every this many lattice cells.
	static constexpr int NOISE_PERIOD = 256;
	// The gust offset wraps at this many gust_scale lengths, a whole period of
	// every noise octave (scales 0.5, 1 and 2), so the wrap is invisible.
	static constexpr float OFFSET_WRAP = 512.0f;

	static constexpr int MAX_ZONES = 64;
	static constexpr int ZONE_TEXELS = 5;

	enum ZoneMode {
		ZONE_DIRECTIONAL,
		ZONE_RADIAL,
		ZONE_VORTEX,
	};

	enum ZoneShape {
		ZONE_SPHERE,
		ZONE_BOX,
	};

	// A local wind zone, in world space.
	struct Zone {
		Vector3 origin;
		// World offset from the origin to the shape's unit space: the shape's
		// edge is at length 1 (sphere) or 1 on any axis (box).
		Basis to_unit;
		// Directional: where it blows. Vortex: the axis it turns around.
		Vector3 axis = Vector3(0.0f, 0.0f, -1.0f);
		float strength = 0.0f;
		// The share of the shape (from the edge in) where it fades in.
		float falloff = 0.25f;
		ZoneMode mode = ZONE_DIRECTIONAL;
		ZoneShape shape = ZONE_SPHERE;
		bool replace = false;
	};

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

	struct ZoneEntry {
		const void *owner = nullptr;
		Zone zone;
	};
	LocalVector<ZoneEntry> zones;
	bool zones_dirty = false;
	// Zones in the texture the shaders read.
	int shader_zone_count = 0;
	Ref<ImageTexture> zone_texture;

	uint64_t last_ticks = 0;
	bool globals_registered = false;
	Ref<ShaderInclude> shader_include;

	void _frame_pre_draw();
	void _update_direction();
	void _push_shader_globals(const State &p_state);
	void _update_zone_texture();

protected:
	static void _bind_methods();

public:
	static WindServer *get_singleton() { return singleton; }
	static void register_project_settings();

	// Global shader parameter names (all vec4).
	static const char *GLOBAL_MEAN; // dir.x, dir.z, speed, gustiness
	static const char *GLOBAL_GUST; // offset.x, offset.z, 1 / gust scale, direction swing
	static const char *GLOBAL_MOTION; // drift, turbulence, zone count, 0
	static const char *GLOBAL_ZONES; // sampler2D, ZONE_TEXELS texels per zone

	// Gust noise, identical to wind_noise() in the shader include.
	static float noise(const Vector2 &p_position);
	static Vector3 sample_state(const State &p_state, const Vector3 &p_position);
	static float gust_state(const State &p_state, const Vector3 &p_position);
	// The zone's wind at a point added to (or replacing) p_wind; identical to
	// wind_zones_apply() in the shader include.
	static Vector3 apply_zone(const Zone &p_zone, const Vector3 &p_position, const Vector3 &p_wind);

	void initialize();
	void finalize();

	State get_state() const;
	Vector3 sample(const Vector3 &p_position) const;
	Vector3 sample_global(const Vector3 &p_position) const;
	void set_zone(const void *p_owner, const Zone &p_zone);
	void remove_zone(const void *p_owner);
	int get_zone_count() const;
	Vector3 get_mean_wind() const;
	float get_gust_factor(const Vector3 &p_position) const;
	float get_gust(const Vector3 &p_position) const;
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
