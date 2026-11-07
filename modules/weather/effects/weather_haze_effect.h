/**************************************************************************/
/*  weather_haze_effect.h                                                 */
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

#include "core/math/vector3.h"

class FogVolume;
class NoiseTexture3D;
class ShaderMaterial;

// Volumetric haze for a Weather node: ground mist that thins out with height
// and drifts in patches, soft rain shafts that lean and drift with the wind,
// and splash spray at the ground in heavy rain. Real haze sits in the lowest
// tens of meters of air, so hilltops and the sky stay clearer than the
// ground around the camera; uniform volumetric fog reads as a flat grey sheet
// instead. While the haze runs, all weather fog goes into it: the
// environment's own volumetric fog density is moved into the haze too.
//
// A FogVolume box follows the camera in world-snapped steps (a volume moving
// every frame smears the fog's temporal filter). Density, height and noise
// are computed in world space, so moving the box never moves the haze. Low
// quality turns the haze off; the lighting effect's uniform fog remains.
class WeatherHazeEffect : public WeatherEffect {
	GDCLASS(WeatherHazeEffect, WeatherEffect);

	static constexpr float DEPTH_BELOW = 20.0f;
	static constexpr float HEIGHT_ABOVE = 100.0f;
	static constexpr float GROUND_RAY_LENGTH = 200.0f;
	// Noise frequency per texel at 64 texels (patches about half a tile wide).
	static constexpr float NOISE_FREQUENCY = 0.035f;

	float density = 0.7f;
	float brightness = 1.0f;
	float distance = 128.0f;
	float sky_affect = 0.75f;
	float rain_density = 0.012f;
	float spray_density = 0.06f;
	float height_falloff = 0.15f;
	float rain_height = 15.0f;
	float patchiness = 0.6f;
	float wisp_strength = 0.45f;
	float patch_size = 96.0f;
	float shaft_evolve_speed = 0.006f;
	float ground_smoothing = 1.0f;
	float camera_height = 1.7f;
	uint32_t ground_mask = UINT32_MAX;

	// Set by the quality level.
	bool quality_enabled = true;
	int noise_resolution = 64;

	Weather *weather = nullptr;
	FogVolume *volume = nullptr;
	Ref<ShaderMaterial> material;
	Ref<NoiseTexture3D> noise_texture;
	float ground = Math::NaN;
	float ground_target = Math::NaN;
	Vector3 drift;
	bool driving = false;

	void _build();
	void _clear();
	void _release_environment();
	void _update_ground(const Vector3 &p_view_position, const Node *p_camera);
	void _update_noise_resolution();
	Vector3 _volume_size() const;

protected:
	static void _bind_methods();

public:
	void set_density(float p_density);
	float get_density() const;
	void set_brightness(float p_brightness);
	float get_brightness() const;
	void set_distance(float p_distance);
	float get_distance() const;
	void set_sky_affect(float p_affect);
	float get_sky_affect() const;
	void set_rain_density(float p_density);
	float get_rain_density() const;
	void set_spray_density(float p_density);
	float get_spray_density() const;
	void set_height_falloff(float p_falloff);
	float get_height_falloff() const;
	void set_rain_height(float p_height);
	float get_rain_height() const;
	void set_patchiness(float p_patchiness);
	float get_patchiness() const;
	void set_wisp_strength(float p_strength);
	float get_wisp_strength() const;
	void set_patch_size(float p_size);
	float get_patch_size() const;
	void set_shaft_evolve_speed(float p_speed);
	float get_shaft_evolve_speed() const;
	void set_ground_smoothing(float p_seconds);
	float get_ground_smoothing() const;
	void set_camera_height(float p_height);
	float get_camera_height() const;
	void set_ground_mask(uint32_t p_mask);
	uint32_t get_ground_mask() const;

	float get_ground_height() const;
	bool is_active() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
	virtual void apply_quality(Weather *p_weather, int p_quality) override;
};
