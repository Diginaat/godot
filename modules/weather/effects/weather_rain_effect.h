/**************************************************************************/
/*  weather_rain_effect.h                                                 */
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
#include "core/math/vector3.h"
#include "core/math/vector4.h"
#include "core/templates/local_vector.h"

class ArrayMesh;
class Camera3D;
class GPUParticles3D;
class GPUParticlesCollisionHeightField3D;
class Material;
class Node3D;
class ParticleProcessMaterial;
class Shader;
class ShaderMaterial;
class SubViewport;
class Texture2D;

// Rain for a Weather node: GPU raindrops around the camera, lit and
// see-through, slanted by the wind; impacts where drops hit geometry (a ring
// on wet surfaces, a crown of droplets); and a rain occlusion map, a
// top-down height map around the camera that keeps surfaces under roofs dry
// (read by res://.builtin/weather_wetness.gdshaderinc).
//
// Only a small volume around the camera is simulated. A height field
// collider (following the camera too) stops drops on the first surface they
// hit. The rain amount scales the live particle count, so a change never
// restarts the rain. Distant rain is the haze effect's job.
class WeatherRainEffect : public WeatherEffect {
	GDCLASS(WeatherRainEffect, WeatherEffect);

	static constexpr float IMPACT_LIFETIME = 0.45f;
	static constexpr uint32_t CAPTURE_LAYER = 1 << 18;

	float wind_multiplier = 0.85f;
	float area_size = 36.0f;
	float emit_height = 14.0f;
	float fall_speed = 11.0f;
	float streak_length = 1.1f;
	float streak_width = 0.005f;
	float opacity = 0.35f;
	float sky_reflection = 1.6f;
	bool impacts_enabled = true;
	bool impact_mist = false;
	float collision_refresh_interval = 0.2f;
	uint32_t rain_layers = 1 << 19;
	bool occlusion_enabled = true;
	float occlusion_area_size = 64.0f;
	float occlusion_update_step = 4.0f;
	float occlusion_capture_height = 60.0f;
	uint32_t cover_layers = 0xFFFFF & ~(1u << 19) & ~CAPTURE_LAYER;

	// Set by the quality level.
	int max_drops = 24000;
	int max_impacts = 8000;
	int droplets_per_impact = 6;
	int collision_resolution = 1; // GPUParticlesCollisionHeightField3D::RESOLUTION_512.
	int occlusion_resolution = 512;

	Weather *weather = nullptr;
	bool dirty = false;
	Node3D *root = nullptr;
	GPUParticles3D *drops = nullptr;
	GPUParticles3D *impacts = nullptr;
	GPUParticlesCollisionHeightField3D *collision = nullptr;
	Ref<ParticleProcessMaterial> process_material;
	Ref<Shader> streak_shader;
	Ref<Shader> impact_shader;
	LocalVector<Ref<ShaderMaterial>> materials;
	float collision_timer = 0.0f;
	float intensity = -1.0f;
	Vector3 wind;

	SubViewport *occlusion_viewport = nullptr;
	Camera3D *occlusion_camera = nullptr;
	Vector2 occlusion_center = Vector2(Math::INF, Math::INF);
	Vector4 occlusion_area;

	void _build();
	void _clear();
	void _build_rain();
	void _build_occlusion();
	GPUParticles3D *_make_particles(int p_amount, float p_lifetime, const Ref<Material> &p_process);
	Ref<ShaderMaterial> _make_impact_material(int p_mode);
	static Ref<ArrayMesh> _make_crown_sheet_mesh(int p_segments, const Ref<Material> &p_material);
	static Ref<ArrayMesh> _make_crown_mesh(int p_droplets, const Ref<Material> &p_material);
	void _update_motion();
	void _update_occlusion(const Vector3 &p_view_position);
	void _set_occlusion_area(const Vector4 &p_area);
	void _changed();

protected:
	static void _bind_methods();

public:
	void set_wind_multiplier(float p_multiplier);
	float get_wind_multiplier() const;
	void set_area_size(float p_size);
	float get_area_size() const;
	void set_emit_height(float p_height);
	float get_emit_height() const;
	void set_fall_speed(float p_speed);
	float get_fall_speed() const;
	void set_streak_length(float p_length);
	float get_streak_length() const;
	void set_streak_width(float p_width);
	float get_streak_width() const;
	void set_opacity(float p_opacity);
	float get_opacity() const;
	void set_sky_reflection(float p_reflection);
	float get_sky_reflection() const;
	void set_impacts_enabled(bool p_enabled);
	bool is_impacts_enabled() const;
	void set_impact_mist(bool p_enabled);
	bool is_impact_mist() const;
	void set_collision_refresh_interval(float p_seconds);
	float get_collision_refresh_interval() const;
	void set_rain_layers(uint32_t p_layers);
	uint32_t get_rain_layers() const;
	void set_occlusion_enabled(bool p_enabled);
	bool is_occlusion_enabled() const;
	void set_occlusion_area_size(float p_size);
	float get_occlusion_area_size() const;
	void set_occlusion_update_step(float p_step);
	float get_occlusion_update_step() const;
	void set_occlusion_capture_height(float p_height);
	float get_occlusion_capture_height() const;
	void set_cover_layers(uint32_t p_layers);
	uint32_t get_cover_layers() const;

	int get_max_drops() const;
	int get_max_impacts() const;
	void refresh_occlusion();
	Vector4 get_occlusion_area() const;
	Ref<Texture2D> get_occlusion_texture() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
	virtual void apply_quality(Weather *p_weather, int p_quality) override;
};
