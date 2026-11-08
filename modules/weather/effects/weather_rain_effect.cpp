/**************************************************************************/
/*  weather_rain_effect.cpp                                               */
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

#include "weather_rain_effect.h"

#include "weather_rain_shaders.h"

#include "../weather.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/particle_process_material.h"
#include "servers/rendering/rendering_server.h"

namespace {

// Per quality level (Low, Medium, High, Ultra): drops and impacts at full
// rain, droplets per impact crown, rain collision and occlusion map
// resolution.
struct RainQuality {
	int drops;
	int impacts;
	int droplets;
	GPUParticlesCollisionHeightField3D::Resolution collision;
	int occlusion;
};

const RainQuality rain_quality[] = {
	{ 8000, 2500, 0, GPUParticlesCollisionHeightField3D::RESOLUTION_256, 256 },
	{ 14000, 5000, 4, GPUParticlesCollisionHeightField3D::RESOLUTION_512, 512 },
	{ 24000, 8000, 6, GPUParticlesCollisionHeightField3D::RESOLUTION_512, 512 },
	{ 40000, 12000, 8, GPUParticlesCollisionHeightField3D::RESOLUTION_1024, 1024 },
};

} // namespace

// Running.

void WeatherRainEffect::start(Weather *p_weather) {
	weather = p_weather;
	_build();
	WeatherEffect::start(p_weather);
}

void WeatherRainEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	_clear();
	weather = nullptr;
}

void WeatherRainEffect::apply_quality(Weather *p_weather, int p_quality) {
	const RainQuality &settings = rain_quality[CLAMP(p_quality, (int)Weather::QUALITY_LOW, (int)Weather::QUALITY_ULTRA) - Weather::QUALITY_LOW];
	if (settings.drops != max_drops || settings.impacts != max_impacts || settings.droplets != droplets_per_impact || settings.collision != collision_resolution || settings.occlusion != occlusion_resolution) {
		max_drops = settings.drops;
		max_impacts = settings.impacts;
		droplets_per_impact = settings.droplets;
		collision_resolution = settings.collision;
		occlusion_resolution = settings.occlusion;
		dirty = true;
	}
	WeatherEffect::apply_quality(p_weather, p_quality);
}

void WeatherRainEffect::update(Weather *p_weather, double p_delta) {
	if (dirty && weather) {
		// Rebuilt pre-simulated, so there's no visible gap.
		_clear();
		_build();
	}
	const Camera3D *camera = p_weather->get_view_camera();
	const Vector3 view_position = camera ? camera->get_global_position() : Vector3();
	if (root && camera) {
		root->set_global_position(view_position);
	}

	const float amount = p_weather->get_rain_amount();
	if (drops && amount != intensity) {
		intensity = amount;
		const bool active = intensity > 0.001f;
		drops->set_amount_ratio(intensity);
		impacts->set_amount_ratio(intensity);
		drops->set_emitting(active);
	}
	const Vector3 ground_wind = p_weather->get_wind();
	const Vector3 new_wind = Vector3(ground_wind.x, 0.0f, ground_wind.z) * wind_multiplier;
	if (!new_wind.is_equal_approx(wind)) {
		wind = new_wind;
		_update_motion();
	}

	// Drops and splashes show the sky's light, so rain reads against dark
	// ground under clouds.
	const Color light = p_weather->get_horizon_light();
	for (const Ref<ShaderMaterial> &material : materials) {
		material->set_shader_parameter(SNAME("sky_light"), Vector3(light.r, light.g, light.b));
	}

	// Moved objects (pushed props, doors, vehicles) must not leave invisible
	// surfaces that drops splash on: re-capture the collider now and then.
	if (collision && collision_refresh_interval > 0.0f && intensity > 0.001f) {
		collision_timer -= (float)p_delta;
		if (collision_timer <= 0.0f) {
			collision_timer = collision_refresh_interval;
			RenderingServer::get_singleton()->particles_collision_height_field_update(collision->get_base());
		}
	}

	if (occlusion_viewport && camera) {
		_update_occlusion(view_position);
	}
	WeatherEffect::update(p_weather, p_delta);
}

void WeatherRainEffect::_update_motion() {
	if (process_material.is_null()) {
		return;
	}
	const Vector3 velocity = Vector3(wind.x, -fall_speed, wind.z);
	process_material->set_direction(velocity.normalized());
	process_material->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, velocity.length() * 0.95f);
	process_material->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, velocity.length() * 1.05f);
	// Spawn upwind, so slanted rain still lands around the camera.
	drops->set_position(Vector3(0.0f, emit_height, 0.0f) - wind * (emit_height / fall_speed));
}

void WeatherRainEffect::_update_occlusion(const Vector3 &p_view_position) {
	// Covers only matter while something is wet: no captures in dry weather.
	const bool needed = weather->get_wetness() > 0.001f || weather->get_rain_amount() > 0.001f;
	if (!needed) {
		if (occlusion_area.w > 0.0f) {
			occlusion_center = Vector2(Math::INF, Math::INF);
			_set_occlusion_area(Vector4());
		}
		return;
	}
	// Re-rendered only after moving a grid step (snapped, so it never shimmers).
	const Vector2 position = Vector2(p_view_position.x, p_view_position.z);
	const Vector2 snapped = position.snapped(Vector2(occlusion_update_step, occlusion_update_step));
	if (snapped != occlusion_center) {
		occlusion_center = snapped;
		occlusion_camera->set_global_position(Vector3(snapped.x, p_view_position.y + occlusion_capture_height, snapped.y));
		refresh_occlusion();
	}
}

void WeatherRainEffect::refresh_occlusion() {
	if (!occlusion_viewport || !Math::is_finite(occlusion_center.x)) {
		return;
	}
	occlusion_viewport->set_update_mode(SubViewport::UPDATE_ONCE);
	_set_occlusion_area(Vector4(occlusion_center.x, occlusion_center.y, occlusion_area_size, 1.0f));
}

void WeatherRainEffect::_set_occlusion_area(const Vector4 &p_area) {
	occlusion_area = p_area;
	RenderingServer::get_singleton()->global_shader_parameter_set(Weather::GLOBAL_OCCLUSION_AREA, p_area);
}

Vector4 WeatherRainEffect::get_occlusion_area() const {
	return occlusion_area;
}

Ref<Texture2D> WeatherRainEffect::get_occlusion_texture() const {
	return occlusion_viewport ? Ref<Texture2D>(occlusion_viewport->get_texture()) : Ref<Texture2D>();
}

// Building.

void WeatherRainEffect::_build() {
	dirty = false;
	intensity = -1.0f;
	wind = Vector3();
	_build_rain();
	if (occlusion_enabled) {
		_build_occlusion();
	}
	_update_motion();
}

void WeatherRainEffect::_clear() {
	// Out of the tree at once, so a rebuild can take the same names.
	if (root) {
		root->get_parent()->remove_child(root);
		root->queue_free();
	}
	if (occlusion_viewport) {
		occlusion_viewport->get_parent()->remove_child(occlusion_viewport);
		occlusion_viewport->queue_free();
		_set_occlusion_area(Vector4());
	}
	root = nullptr;
	drops = nullptr;
	impacts = nullptr;
	collision = nullptr;
	occlusion_viewport = nullptr;
	occlusion_camera = nullptr;
	occlusion_center = Vector2(Math::INF, Math::INF);
	process_material.unref();
	materials.clear();
}

GPUParticles3D *WeatherRainEffect::_make_particles(int p_amount, float p_lifetime, const Ref<Material> &p_process) {
	GPUParticles3D *particles = memnew(GPUParticles3D);
	particles->set_amount(MAX(p_amount, 1));
	particles->set_lifetime(p_lifetime);
	particles->set_use_local_coordinates(false);
	particles->set_fixed_fps(0);
	particles->set_process_material(p_process);
	particles->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	particles->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	particles->set_layer_mask(rain_layers);
	return particles;
}

Ref<ShaderMaterial> WeatherRainEffect::_make_impact_material(int p_mode) {
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(impact_shader);
	material->set_shader_parameter("mode", p_mode);
	material->set_shader_parameter("lifetime", IMPACT_LIFETIME);
	materials.push_back(material);
	return material;
}

void WeatherRainEffect::_build_rain() {
	if (streak_shader.is_null()) {
		streak_shader.instantiate();
		streak_shader->set_code(weather_rain_streak_shader_code);
		impact_shader.instantiate();
		impact_shader->set_code(weather_rain_impact_shader_code);
	}

	root = memnew(Node3D);
	root->set_name("Rain");
	root->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	weather->add_child(root, false, Node::INTERNAL_MODE_FRONT);

	process_material.instantiate();
	process_material->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	process_material->set_emission_box_extents(Vector3(area_size * 0.5f, 0.5f, area_size * 0.5f));
	process_material->set_gravity(Vector3());
	process_material->set_spread(1.5f);
	// Drop size variation (the streak shader also varies each drop).
	process_material->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.7f);
	process_material->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.3f);
	process_material->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
	process_material->set_collision_mode(ParticleProcessMaterial::COLLISION_HIDE_ON_CONTACT);
	if (impacts_enabled) {
		process_material->set_sub_emitter_mode(ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION);
		process_material->set_sub_emitter_amount_at_collision(1);
	}

	Ref<ShaderMaterial> drop_material;
	drop_material.instantiate();
	drop_material->set_shader(streak_shader);
	drop_material->set_shader_parameter("streak_length", streak_length);
	drop_material->set_shader_parameter("streak_width", streak_width);
	drop_material->set_shader_parameter("opacity", opacity);
	drop_material->set_shader_parameter("far_fade", area_size * 0.9f);
	drop_material->set_shader_parameter("sky_reflection", sky_reflection);
	drop_material->set_shader_parameter("emission", emission);
	materials.push_back(drop_material);
	Ref<QuadMesh> drop_mesh;
	drop_mesh.instantiate();
	drop_mesh->set_material(drop_material);

	drops = _make_particles(max_drops, (emit_height + 6.0f) / fall_speed, process_material);
	drops->set_name("Drops");
	drops->set_draw_pass_mesh(0, drop_mesh);
	const AABB visibility = AABB(Vector3(-area_size, -emit_height - 10.0f, -area_size), Vector3(area_size * 2.0f, emit_height + 12.0f, area_size * 2.0f));
	drops->set_visibility_aabb(visibility);
	drops->set_collision_base_size(0.02f);
	// Start pre-simulated: no front of rain falling from the sky when it begins.
	drops->set_pre_process_time(drops->get_lifetime());
	root->add_child(drops);

	// Impacts don't move; their shader animates the ring and droplets.
	Ref<ParticleProcessMaterial> impact_process;
	impact_process.instantiate();
	impact_process->set_gravity(Vector3());
	impact_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.0f);
	impact_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.0f);
	impacts = _make_particles(max_impacts, IMPACT_LIFETIME, impact_process);
	impacts->set_name("Impacts");
	impacts->set_visibility_aabb(visibility);
	Vector<Ref<Mesh>> passes;
	Ref<PlaneMesh> ring_mesh;
	ring_mesh.instantiate();
	ring_mesh->set_size(Vector2(1.0f, 1.0f));
	ring_mesh->set_material(_make_impact_material(0));
	passes.push_back(ring_mesh);
	passes.push_back(_make_crown_sheet_mesh(24, _make_impact_material(3)));
	if (droplets_per_impact > 0) {
		passes.push_back(_make_crown_mesh(droplets_per_impact, _make_impact_material(1)));
	}
	if (impact_mist) {
		Ref<QuadMesh> mist_mesh;
		mist_mesh.instantiate();
		mist_mesh->set_material(_make_impact_material(2));
		passes.push_back(mist_mesh);
	}
	impacts->set_draw_passes(passes.size());
	for (int i = 0; i < passes.size(); i++) {
		impacts->set_draw_pass_mesh(i, passes[i]);
	}
	root->add_child(impacts);
	if (impacts_enabled) {
		drops->set_sub_emitter(drops->get_path_to(impacts));
	}

	collision = memnew(GPUParticlesCollisionHeightField3D);
	collision->set_name("Collision");
	collision->set_size(Vector3(area_size, emit_height + 12.0f, area_size));
	collision->set_resolution((GPUParticlesCollisionHeightField3D::Resolution)collision_resolution);
	collision->set_update_mode(GPUParticlesCollisionHeightField3D::UPDATE_MODE_WHEN_MOVED);
	collision->set_follow_camera_enabled(true);
	// cull_mask: which particles collide; heightfield_mask: which geometry is recorded.
	collision->set_cull_mask(rain_layers);
	collision->set_heightfield_mask(0xFFFFF & ~rain_layers);
	root->add_child(collision);
}

void WeatherRainEffect::_build_occlusion() {
	occlusion_viewport = memnew(SubViewport);
	occlusion_viewport->set_name("RainOcclusion");
	occlusion_viewport->set_size(Size2i(occlusion_resolution, occlusion_resolution));
	// Float texture: exact heights in meters.
	occlusion_viewport->set_use_hdr_2d(true);
	occlusion_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	occlusion_viewport->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);
	// Exact depth: no upscaler, no temporal jitter, no AA blur, no shadows.
	occlusion_viewport->set_scaling_3d_mode(Viewport::SCALING_3D_MODE_BILINEAR);
	occlusion_viewport->set_scaling_3d_scale(1.0f);
	occlusion_viewport->set_use_taa(false);
	occlusion_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
	occlusion_viewport->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_DISABLED);
	occlusion_viewport->set_positional_shadow_atlas_size(0);
	weather->add_child(occlusion_viewport, false, Node::INTERNAL_MODE_FRONT);

	occlusion_camera = memnew(Camera3D);
	// Never interpolated: a blend between poses would tilt the capture.
	occlusion_camera->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	occlusion_camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
	occlusion_camera->set_size(occlusion_area_size);
	occlusion_camera->set_near(0.5f);
	occlusion_camera->set_far(occlusion_capture_height + 200.0f);
	occlusion_camera->set_cull_mask(cover_layers | CAPTURE_LAYER);
	// Straight down; screen up is world -Z.
	occlusion_camera->set_rotation(Vector3(-Math::PI * 0.5f, 0.0f, 0.0f));
	// Plain output: tonemap, fog, glow and exposure must not touch the heights.
	Ref<Environment> environment;
	environment.instantiate();
	environment->set_background(Environment::BG_COLOR);
	environment->set_bg_color(Color(-1000.0f, -1000.0f, -1000.0f));
	environment->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	occlusion_camera->set_environment(environment);
	Ref<CameraAttributesPractical> attributes;
	attributes.instantiate();
	occlusion_camera->set_attributes(attributes);
	occlusion_viewport->add_child(occlusion_camera);

	Ref<Shader> capture_shader;
	capture_shader.instantiate();
	capture_shader->set_code(weather_rain_capture_shader_code);
	Ref<ShaderMaterial> capture_material;
	capture_material.instantiate();
	capture_material->set_shader(capture_shader);
	Ref<QuadMesh> quad_mesh;
	quad_mesh.instantiate();
	quad_mesh->set_material(capture_material);
	MeshInstance3D *quad = memnew(MeshInstance3D);
	quad->set_mesh(quad_mesh);
	quad->set_layer_mask(CAPTURE_LAYER);
	quad->set_extra_cull_margin(16384.0f);
	quad->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	quad->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	occlusion_camera->add_child(quad);
	quad->set_position(Vector3(0.0f, 0.0f, -1.0f));

	RenderingServer::get_singleton()->global_shader_parameter_set(Weather::GLOBAL_RAIN_OCCLUSION, occlusion_viewport->get_texture());
}

// Open cylinder for the crown sheet; UV.x is the angle (0 to 1), UV.y the
// height (0 to 1). The shader shapes and animates it.
Ref<ArrayMesh> WeatherRainEffect::_make_crown_sheet_mesh(int p_segments, const Ref<Material> &p_material) {
	PackedVector3Array vertices;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	for (int i = 0; i <= p_segments; i++) {
		const float u = (float)i / p_segments;
		for (int v = 0; v < 2; v++) {
			vertices.push_back(Vector3(Math::cos(u * Math::TAU), (float)v, Math::sin(u * Math::TAU)));
			uvs.push_back(Vector2(u, (float)v));
		}
	}
	for (int i = 0; i < p_segments; i++) {
		const int a = i * 2;
		const int quad[6] = { a, a + 2, a + 1, a + 1, a + 2, a + 3 };
		for (int index : quad) {
			indices.push_back(index);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, p_material);
	mesh->set_custom_aabb(AABB(Vector3(-0.2f, -0.05f, -0.2f), Vector3(0.4f, 0.3f, 0.4f)));
	return mesh;
}

// One small quad per droplet; COLOR.r is the droplet's index (0 to 1).
Ref<ArrayMesh> WeatherRainEffect::_make_crown_mesh(int p_droplets, const Ref<Material> &p_material) {
	PackedVector3Array vertices;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	const Vector2 corners[4] = { Vector2(-0.5f, -0.5f), Vector2(0.5f, -0.5f), Vector2(0.5f, 0.5f), Vector2(-0.5f, 0.5f) };
	for (int i = 0; i < p_droplets; i++) {
		const Color index_color = Color((float)i / p_droplets, 0.0f, 0.0f);
		const int base = vertices.size();
		for (const Vector2 &corner : corners) {
			vertices.push_back(Vector3(corner.x, corner.y, 0.0f));
			uvs.push_back(corner + Vector2(0.5f, 0.5f));
			colors.push_back(index_color);
		}
		const int quad[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
		for (int index : quad) {
			indices.push_back(index);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, p_material);
	// The shader moves droplets up to about 0.4 m from the particle.
	mesh->set_custom_aabb(AABB(Vector3(-0.5f, -0.1f, -0.5f), Vector3(1.0f, 0.6f, 1.0f)));
	return mesh;
}

// Settings.

void WeatherRainEffect::_changed() {
	// Rebuilt on the next update while running.
	dirty = true;
	emit_changed();
}

void WeatherRainEffect::set_wind_multiplier(float p_multiplier) {
	wind_multiplier = p_multiplier;
	emit_changed();
}

float WeatherRainEffect::get_wind_multiplier() const {
	return wind_multiplier;
}

void WeatherRainEffect::set_area_size(float p_size) {
	area_size = MAX(1.0f, p_size);
	_changed();
}

float WeatherRainEffect::get_area_size() const {
	return area_size;
}

void WeatherRainEffect::set_emit_height(float p_height) {
	emit_height = MAX(1.0f, p_height);
	_changed();
}

float WeatherRainEffect::get_emit_height() const {
	return emit_height;
}

void WeatherRainEffect::set_fall_speed(float p_speed) {
	fall_speed = MAX(0.1f, p_speed);
	_changed();
}

float WeatherRainEffect::get_fall_speed() const {
	return fall_speed;
}

void WeatherRainEffect::set_streak_length(float p_length) {
	streak_length = MAX(0.0f, p_length);
	_changed();
}

float WeatherRainEffect::get_streak_length() const {
	return streak_length;
}

void WeatherRainEffect::set_streak_width(float p_width) {
	streak_width = MAX(0.0f, p_width);
	_changed();
}

float WeatherRainEffect::get_streak_width() const {
	return streak_width;
}

void WeatherRainEffect::set_opacity(float p_opacity) {
	opacity = CLAMP(p_opacity, 0.0f, 1.0f);
	_changed();
}

float WeatherRainEffect::get_opacity() const {
	return opacity;
}

void WeatherRainEffect::set_sky_reflection(float p_reflection) {
	sky_reflection = MAX(0.0f, p_reflection);
	_changed();
}

float WeatherRainEffect::get_sky_reflection() const {
	return sky_reflection;
}

void WeatherRainEffect::set_emission(float p_emission) {
	emission = MAX(0.0f, p_emission);
	_changed();
}

float WeatherRainEffect::get_emission() const {
	return emission;
}

void WeatherRainEffect::set_impacts_enabled(bool p_enabled) {
	impacts_enabled = p_enabled;
	_changed();
}

bool WeatherRainEffect::is_impacts_enabled() const {
	return impacts_enabled;
}

void WeatherRainEffect::set_impact_mist(bool p_enabled) {
	impact_mist = p_enabled;
	_changed();
}

bool WeatherRainEffect::is_impact_mist() const {
	return impact_mist;
}

void WeatherRainEffect::set_collision_refresh_interval(float p_seconds) {
	collision_refresh_interval = MAX(0.0f, p_seconds);
	emit_changed();
}

float WeatherRainEffect::get_collision_refresh_interval() const {
	return collision_refresh_interval;
}

void WeatherRainEffect::set_rain_layers(uint32_t p_layers) {
	rain_layers = p_layers;
	_changed();
}

uint32_t WeatherRainEffect::get_rain_layers() const {
	return rain_layers;
}

void WeatherRainEffect::set_occlusion_enabled(bool p_enabled) {
	occlusion_enabled = p_enabled;
	_changed();
}

bool WeatherRainEffect::is_occlusion_enabled() const {
	return occlusion_enabled;
}

void WeatherRainEffect::set_occlusion_area_size(float p_size) {
	occlusion_area_size = MAX(1.0f, p_size);
	_changed();
}

float WeatherRainEffect::get_occlusion_area_size() const {
	return occlusion_area_size;
}

void WeatherRainEffect::set_occlusion_update_step(float p_step) {
	occlusion_update_step = MAX(0.01f, p_step);
	emit_changed();
}

float WeatherRainEffect::get_occlusion_update_step() const {
	return occlusion_update_step;
}

void WeatherRainEffect::set_occlusion_capture_height(float p_height) {
	occlusion_capture_height = MAX(1.0f, p_height);
	_changed();
}

float WeatherRainEffect::get_occlusion_capture_height() const {
	return occlusion_capture_height;
}

void WeatherRainEffect::set_cover_layers(uint32_t p_layers) {
	cover_layers = p_layers;
	_changed();
}

uint32_t WeatherRainEffect::get_cover_layers() const {
	return cover_layers;
}

int WeatherRainEffect::get_max_drops() const {
	return max_drops;
}

int WeatherRainEffect::get_max_impacts() const {
	return max_impacts;
}

void WeatherRainEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_wind_multiplier", "multiplier"), &WeatherRainEffect::set_wind_multiplier);
	ClassDB::bind_method(D_METHOD("get_wind_multiplier"), &WeatherRainEffect::get_wind_multiplier);
	ClassDB::bind_method(D_METHOD("set_area_size", "size"), &WeatherRainEffect::set_area_size);
	ClassDB::bind_method(D_METHOD("get_area_size"), &WeatherRainEffect::get_area_size);
	ClassDB::bind_method(D_METHOD("set_emit_height", "height"), &WeatherRainEffect::set_emit_height);
	ClassDB::bind_method(D_METHOD("get_emit_height"), &WeatherRainEffect::get_emit_height);
	ClassDB::bind_method(D_METHOD("set_fall_speed", "speed"), &WeatherRainEffect::set_fall_speed);
	ClassDB::bind_method(D_METHOD("get_fall_speed"), &WeatherRainEffect::get_fall_speed);
	ClassDB::bind_method(D_METHOD("set_streak_length", "length"), &WeatherRainEffect::set_streak_length);
	ClassDB::bind_method(D_METHOD("get_streak_length"), &WeatherRainEffect::get_streak_length);
	ClassDB::bind_method(D_METHOD("set_streak_width", "width"), &WeatherRainEffect::set_streak_width);
	ClassDB::bind_method(D_METHOD("get_streak_width"), &WeatherRainEffect::get_streak_width);
	ClassDB::bind_method(D_METHOD("set_opacity", "opacity"), &WeatherRainEffect::set_opacity);
	ClassDB::bind_method(D_METHOD("get_opacity"), &WeatherRainEffect::get_opacity);
	ClassDB::bind_method(D_METHOD("set_sky_reflection", "reflection"), &WeatherRainEffect::set_sky_reflection);
	ClassDB::bind_method(D_METHOD("get_sky_reflection"), &WeatherRainEffect::get_sky_reflection);
	ClassDB::bind_method(D_METHOD("set_emission", "emission"), &WeatherRainEffect::set_emission);
	ClassDB::bind_method(D_METHOD("get_emission"), &WeatherRainEffect::get_emission);
	ClassDB::bind_method(D_METHOD("set_impacts_enabled", "enabled"), &WeatherRainEffect::set_impacts_enabled);
	ClassDB::bind_method(D_METHOD("is_impacts_enabled"), &WeatherRainEffect::is_impacts_enabled);
	ClassDB::bind_method(D_METHOD("set_impact_mist", "enabled"), &WeatherRainEffect::set_impact_mist);
	ClassDB::bind_method(D_METHOD("is_impact_mist"), &WeatherRainEffect::is_impact_mist);
	ClassDB::bind_method(D_METHOD("set_collision_refresh_interval", "seconds"), &WeatherRainEffect::set_collision_refresh_interval);
	ClassDB::bind_method(D_METHOD("get_collision_refresh_interval"), &WeatherRainEffect::get_collision_refresh_interval);
	ClassDB::bind_method(D_METHOD("set_rain_layers", "layers"), &WeatherRainEffect::set_rain_layers);
	ClassDB::bind_method(D_METHOD("get_rain_layers"), &WeatherRainEffect::get_rain_layers);
	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &WeatherRainEffect::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("is_occlusion_enabled"), &WeatherRainEffect::is_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_occlusion_area_size", "size"), &WeatherRainEffect::set_occlusion_area_size);
	ClassDB::bind_method(D_METHOD("get_occlusion_area_size"), &WeatherRainEffect::get_occlusion_area_size);
	ClassDB::bind_method(D_METHOD("set_occlusion_update_step", "step"), &WeatherRainEffect::set_occlusion_update_step);
	ClassDB::bind_method(D_METHOD("get_occlusion_update_step"), &WeatherRainEffect::get_occlusion_update_step);
	ClassDB::bind_method(D_METHOD("set_occlusion_capture_height", "height"), &WeatherRainEffect::set_occlusion_capture_height);
	ClassDB::bind_method(D_METHOD("get_occlusion_capture_height"), &WeatherRainEffect::get_occlusion_capture_height);
	ClassDB::bind_method(D_METHOD("set_cover_layers", "layers"), &WeatherRainEffect::set_cover_layers);
	ClassDB::bind_method(D_METHOD("get_cover_layers"), &WeatherRainEffect::get_cover_layers);
	ClassDB::bind_method(D_METHOD("get_max_drops"), &WeatherRainEffect::get_max_drops);
	ClassDB::bind_method(D_METHOD("get_max_impacts"), &WeatherRainEffect::get_max_impacts);
	ClassDB::bind_method(D_METHOD("refresh_occlusion"), &WeatherRainEffect::refresh_occlusion);
	ClassDB::bind_method(D_METHOD("get_occlusion_texture"), &WeatherRainEffect::get_occlusion_texture);
	ClassDB::bind_method(D_METHOD("get_occlusion_area"), &WeatherRainEffect::get_occlusion_area);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_multiplier", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_wind_multiplier", "get_wind_multiplier");
	ADD_GROUP("Volume", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "area_size", PROPERTY_HINT_RANGE, "4,200,0.1,or_greater,suffix:m"), "set_area_size", "get_area_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emit_height", PROPERTY_HINT_RANGE, "1,100,0.1,or_greater,suffix:m"), "set_emit_height", "get_emit_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_speed", PROPERTY_HINT_RANGE, "0.1,30,0.1,or_greater,suffix:m/s"), "set_fall_speed", "get_fall_speed");
	ADD_GROUP("Look", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streak_length", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater,suffix:m"), "set_streak_length", "get_streak_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streak_width", PROPERTY_HINT_RANGE, "0,0.05,0.0001,or_greater,suffix:m"), "set_streak_width", "get_streak_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_opacity", "get_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_reflection", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sky_reflection", "get_sky_reflection");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emission", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_emission", "get_emission");
	ADD_GROUP("Impacts", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "impacts_enabled"), "set_impacts_enabled", "is_impacts_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "impact_mist"), "set_impact_mist", "is_impact_mist");
	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_refresh_interval", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:s"), "set_collision_refresh_interval", "get_collision_refresh_interval");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rain_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_rain_layers", "get_rain_layers");
	ADD_GROUP("Occlusion", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_enabled"), "set_occlusion_enabled", "is_occlusion_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_area_size", PROPERTY_HINT_RANGE, "8,512,0.1,or_greater,suffix:m"), "set_occlusion_area_size", "get_occlusion_area_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_update_step", PROPERTY_HINT_RANGE, "0.1,32,0.1,or_greater,suffix:m"), "set_occlusion_update_step", "get_occlusion_update_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_capture_height", PROPERTY_HINT_RANGE, "1,500,0.1,or_greater,suffix:m"), "set_occlusion_capture_height", "get_occlusion_capture_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cover_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_cover_layers", "get_cover_layers");
}
