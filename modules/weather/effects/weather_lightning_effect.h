/**************************************************************************/
/*  weather_lightning_effect.h                                            */
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

#include "../weather_effect.h"

#include "core/math/color.h"
#include "core/math/random_pcg.h"
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"

class ArrayMesh;
class DirectionalLight3D;
class MeshInstance3D;
class ShaderMaterial;

// Lightning for a Weather node, at the weather's lightning_per_minute (in game
// only; strike() works in the editor too). Each flash is a few return strokes
// within about a second, which is the flicker of real lightning: each lights
// the scene through a directional light from the strike, and the sky near the
// strike through the sky effect. Some flashes are cloud-to-ground bolts, the
// rest light the clouds from inside, as most real lightning does.
//
// The bolt channel is built by midpoint displacement: each segment is split at
// its midpoint, which is pushed sideways at random, with half the push at every
// level. That gives the jagged, self-similar shape of real lightning. Branches
// split off the upper part and half of them fork once more. A faint leader
// grows down from the cloud before the first stroke. Thunder is left to the
// sound: the Weather node emits lightning_struck with the distance.
class WeatherLightningEffect : public WeatherEffect {
	GDCLASS(WeatherLightningEffect, WeatherEffect);

	float energy = 2.5f;
	Color color = Color(0.78f, 0.84f, 1.0f);
	float distance_min = 300.0f;
	float distance_max = 3000.0f;
	float ground_strike_chance = 0.4f;
	float cloud_base_height = 1200.0f;
	Color glow_color = Color(0.55f, 0.62f, 1.0f);
	int bolt_levels = 7;
	float bolt_roughness = 0.22f;
	int max_branches = 6;
	float preview_hold_time = 8.0f;
	uint32_t bolt_layers = 1 << 19;

	struct Polyline {
		LocalVector<Vector3> points;
		float width = 1.0f;
		float brightness = 1.0f;
		// Where it starts down the main channel, and how far it reaches.
		float start = 0.0f;
		float span = 1.0f;
	};

	Weather *weather = nullptr;
	DirectionalLight3D *light = nullptr;
	MeshInstance3D *bolt = nullptr;
	Ref<ShaderMaterial> bolt_material;
	RandomPCG rng;

	double next_strike = 0.0;
	// Seconds into the current flash, negative when there is none.
	double flash_time = -1.0;
	// Return strokes of the current flash: (time, strength).
	LocalVector<Vector2> strokes;
	// Seconds the leader takes to reach the ground (0: no bolt).
	float leader_time = 0.0f;
	float flash_strength = 1.0f;
	float bolt_visibility = 1.0f;
	// Keep the bolt lit until this many seconds into the flash (preview).
	double hold_until = 0.0;
	Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);
	float flash = 0.0f;

	LocalVector<Vector3> _channel(const Vector3 &p_from, const Vector3 &p_to, int p_levels, float p_roughness);
	Ref<ArrayMesh> _build_mesh(const LocalVector<Polyline> &p_polylines) const;
	void _build_bolt(const Vector3 &p_top, const Vector3 &p_bottom);
	void _show_bolt(float p_flash, float p_branch_flash, float p_reveal);
	void _clear();
	Callable _get_strike_button();

protected:
	static void _bind_methods();

public:
	void set_energy(float p_energy);
	float get_energy() const;
	void set_color(const Color &p_color);
	Color get_color() const;
	void set_distance_min(float p_distance);
	float get_distance_min() const;
	void set_distance_max(float p_distance);
	float get_distance_max() const;
	void set_ground_strike_chance(float p_chance);
	float get_ground_strike_chance() const;
	void set_cloud_base_height(float p_height);
	float get_cloud_base_height() const;
	void set_glow_color(const Color &p_color);
	Color get_glow_color() const;
	void set_bolt_levels(int p_levels);
	int get_bolt_levels() const;
	void set_bolt_roughness(float p_roughness);
	float get_bolt_roughness() const;
	void set_max_branches(int p_branches);
	int get_max_branches() const;
	void set_preview_hold_time(float p_seconds);
	float get_preview_hold_time() const;
	void set_bolt_layers(uint32_t p_layers);
	uint32_t get_bolt_layers() const;

	void strike(float p_distance = -1.0f, int p_ground_strike = -1);
	void preview_strike();
	float get_flash() const;
	bool is_bolt_visible() const;
	Vector3 get_direction() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
};
