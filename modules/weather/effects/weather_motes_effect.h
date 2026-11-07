/**************************************************************************/
/*  weather_motes_effect.h                                                */
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

#include "core/math/vector2.h"

class GPUParticles3D;
class ShaderMaterial;

// Tiny droplets floating in the air around the camera, drifting with the
// wind and swirling, so fog and rain read as countless small particles
// instead of one smooth wash. Volumetric fog can't show detail this fine
// (its froxels are decimeters to meters wide). They are lit by the scene's
// lights, glitter in light beams, and take the sky's light under clouds.
//
// The motes drift with the wind in the shader and wrap around inside a box
// around the camera, so they follow any wind at once and the box stays
// full. Their number follows the fog and the rain; Low quality has none.
class WeatherMotesEffect : public WeatherEffect {
	GDCLASS(WeatherMotesEffect, WeatherEffect);

	float amount = 1.0f;
	float radius = 14.0f;
	float height = 6.0f;
	float lifetime = 5.0f;
	float wind_drift = 0.06f;
	float max_drift_speed = 1.5f;
	uint32_t mote_layers = 1 << 19;

	// Set by the quality level: motes at full fog and rain.
	int max_motes = 6000;

	Weather *weather = nullptr;
	bool dirty = false;
	GPUParticles3D *particles = nullptr;
	Ref<ShaderMaterial> material;
	Vector2 wind_offset;
	float intensity = -1.0f;

	void _build();
	void _clear();
	void _changed();

protected:
	static void _bind_methods();

public:
	void set_amount(float p_amount);
	float get_amount() const;
	void set_radius(float p_radius);
	float get_radius() const;
	void set_height(float p_height);
	float get_height() const;
	void set_lifetime(float p_seconds);
	float get_lifetime() const;
	void set_wind_drift(float p_drift);
	float get_wind_drift() const;
	void set_max_drift_speed(float p_speed);
	float get_max_drift_speed() const;
	void set_mote_layers(uint32_t p_layers);
	uint32_t get_mote_layers() const;

	int get_max_motes() const;
	float get_intensity() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
	virtual void apply_quality(Weather *p_weather, int p_quality) override;
};
