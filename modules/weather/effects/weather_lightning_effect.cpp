/**************************************************************************/
/*  weather_lightning_effect.cpp                                          */
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

#include "weather_lightning_effect.h"

#include "weather_lightning_shader.h"
#include "weather_sky_effect.h"

#include "../weather.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

void WeatherLightningEffect::start(Weather *p_weather) {
	weather = p_weather;
	rng.randomize();
	next_strike = rng.random(2.0f, 6.0f);
	flash_time = -1.0;
	flash = 0.0f;

	light = memnew(DirectionalLight3D);
	light->set_name("LightningLight");
	light->set_color(color);
	light->set_param(Light3D::PARAM_ENERGY, 0.0f);
	light->set_shadow(false);
	// Lights the scene and the fog, but adds no second sun to the sky.
	light->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
	light->set_visible(false);
	p_weather->add_child(light, false, Node::INTERNAL_MODE_FRONT);

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(weather_lightning_shader_code);
	bolt_material.instantiate();
	bolt_material->set_shader(shader);
	bolt_material->set_shader_parameter("glow_color", glow_color);
	bolt = memnew(MeshInstance3D);
	bolt->set_name("LightningBolt");
	bolt->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	bolt->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	bolt->set_material_override(bolt_material);
	bolt->set_layer_mask(bolt_layers);
	bolt->set_visible(false);
	p_weather->add_child(bolt, false, Node::INTERNAL_MODE_FRONT);

	WeatherEffect::start(p_weather);
}

void WeatherLightningEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	// The sky keeps running without lightning.
	const Ref<WeatherSkyEffect> sky = p_weather->get_effect(SNAME("WeatherSkyEffect"));
	if (sky.is_valid()) {
		sky->set_lightning(p_weather, 0.0f, direction, color);
	}
	_clear();
	weather = nullptr;
}

void WeatherLightningEffect::_clear() {
	// Out of the tree at once, so a restart can take the same names.
	for (Node *node : { (Node *)light, (Node *)bolt }) {
		if (node) {
			node->get_parent()->remove_child(node);
			node->queue_free();
		}
	}
	light = nullptr;
	bolt = nullptr;
	bolt_material.unref();
	flash_time = -1.0;
	flash = 0.0f;
}

// The bolt.

LocalVector<Vector3> WeatherLightningEffect::_channel(const Vector3 &p_from, const Vector3 &p_to, int p_levels, float p_roughness) {
	LocalVector<Vector3> points;
	points.push_back(p_from);
	points.push_back(p_to);
	float push = p_from.distance_to(p_to) * p_roughness;
	for (int level = 0; level < p_levels; level++) {
		LocalVector<Vector3> next;
		next.reserve(points.size() * 2 - 1);
		for (uint32_t i = 0; i + 1 < points.size(); i++) {
			const Vector3 &p = points[i];
			const Vector3 &q = points[i + 1];
			next.push_back(p);
			const Vector3 along = (q - p).normalized();
			const Vector3 reference = Math::abs(along.y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
			const Vector3 side_a = along.cross(reference).normalized();
			const Vector3 side_b = along.cross(side_a);
			next.push_back((p + q) * 0.5f + (side_a * rng.random(-1.0f, 1.0f) + side_b * rng.random(-1.0f, 1.0f)) * push);
		}
		next.push_back(points[points.size() - 1]);
		points = next;
		push *= 0.5f;
	}
	return points;
}

Ref<ArrayMesh> WeatherLightningEffect::_build_mesh(const LocalVector<Polyline> &p_polylines) const {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	PackedInt32Array indices;
	for (const Polyline &line : p_polylines) {
		const int count = line.points.size();
		const int base = vertices.size();
		for (int i = 0; i < count; i++) {
			const Vector3 along = (line.points[MIN(i + 1, count - 1)] - line.points[MAX(i - 1, 0)]).normalized();
			const float progress = line.start + float(i) / (count - 1) * line.span;
			// Two vertices per point: the shader widens them into a ribbon.
			for (const float side : { -1.0f, 1.0f }) {
				vertices.push_back(line.points[i]);
				normals.push_back(along);
				uvs.push_back(Vector2(side, progress));
				uv2s.push_back(Vector2(line.width, line.brightness));
			}
		}
		for (int i = 0; i < count - 1; i++) {
			const int v = base + i * 2;
			for (const int index : { v, v + 1, v + 2, v + 1, v + 3, v + 2 }) {
				indices.push_back(index);
			}
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	// The shader widens the ribbons: keep the bounds from culling them.
	mesh->set_custom_aabb(mesh->get_aabb().grow(50.0f));
	return mesh;
}

void WeatherLightningEffect::_build_bolt(const Vector3 &p_top, const Vector3 &p_bottom) {
	LocalVector<Polyline> polylines;
	Polyline main;
	main.points = _channel(p_top, p_bottom, bolt_levels, bolt_roughness);
	const LocalVector<Vector3> &channel = main.points;

	const float total_length = p_top.distance_to(p_bottom);
	const int branches = rng.random(max_branches / 2, max_branches);
	for (int i = 0; i < branches; i++) {
		// Branches leave the upper 70% of the channel, heading out and down.
		const float at = rng.random(0.05f, 0.7f);
		const int index = int(at * (channel.size() - 1));
		const Vector3 start = channel[index];
		const Vector3 down = (channel[MIN(index + 4, (int)channel.size() - 1)] - start).normalized();
		const Vector3 out = Vector3(rng.random(-1.0f, 1.0f), 0.0f, rng.random(-1.0f, 1.0f)).normalized();
		const Vector3 heading = (down + out * rng.random(0.5f, 1.3f)).normalized();
		const float length = total_length * (1.0f - at) * rng.random(0.15f, 0.4f);
		Polyline branch;
		branch.points = _channel(start, start + heading * length, MAX(bolt_levels - 2, 1), bolt_roughness * 1.2f);
		branch.width = 0.45f;
		branch.brightness = 0.55f;
		branch.start = at;
		branch.span = (1.0f - at) * 0.5f;
		// Half the branches fork once more.
		if (rng.randf() < 0.5f) {
			const int size = branch.points.size();
			const int fork_at = rng.random(size / 4, size * 3 / 4);
			const Vector3 fork_start = branch.points[fork_at];
			const Vector3 fork_heading = (heading + Vector3(rng.random(-1.0f, 1.0f), -0.3f, rng.random(-1.0f, 1.0f)) * 0.8f).normalized();
			Polyline fork;
			fork.points = _channel(fork_start, fork_start + fork_heading * length * 0.5f, MAX(bolt_levels - 3, 1), bolt_roughness * 1.3f);
			fork.width = 0.25f;
			fork.brightness = 0.35f;
			fork.start = at + float(fork_at) / (size - 1) * branch.span;
			fork.span = (1.0f - fork.start) * 0.4f;
			polylines.push_back(fork);
		}
		polylines.push_back(branch);
	}
	polylines.push_back(main);
	bolt->set_mesh(_build_mesh(polylines));
	bolt->set_visible(false);
}

void WeatherLightningEffect::_show_bolt(float p_flash, float p_branch_flash, float p_reveal) {
	bolt->set_visible(p_flash > 0.002f);
	if (!bolt->is_visible()) {
		return;
	}
	bolt_material->set_shader_parameter(SNAME("flash"), p_flash);
	bolt_material->set_shader_parameter(SNAME("branch_flash"), p_branch_flash);
	bolt_material->set_shader_parameter(SNAME("reveal"), p_reveal);
	bolt_material->set_shader_parameter(SNAME("visibility"), bolt_visibility);
}

// Flashes.

void WeatherLightningEffect::strike(float p_distance, int p_ground_strike) {
	ERR_FAIL_NULL_MSG(weather, "The lightning effect isn't running: add it to a Weather node's effects.");
	const float distance = p_distance < 0.0f ? rng.random(distance_min, MAX(distance_min, distance_max)) : p_distance;
	const bool to_ground = p_ground_strike < 0 ? rng.randf() < ground_strike_chance : p_ground_strike > 0;
	const Camera3D *camera = weather->get_view_camera();
	const Vector3 origin = camera ? camera->get_global_position() : Vector3();
	const float azimuth = rng.randf() * Math::TAU;

	// Most flashes are several return strokes down the same channel within
	// about a second: that's the flicker. The first is the brightest.
	strokes.clear();
	float time = 0.0f;
	const int count = rng.random(1, 4);
	for (int i = 0; i < count; i++) {
		strokes.push_back(Vector2(time, i == 0 ? 1.0f : rng.random(0.45f, 0.9f)));
		time += rng.random(0.04f, 0.12f);
	}

	leader_time = 0.0f;
	Vector3 position;
	if (to_ground) {
		Vector3 ground = origin + Vector3(Math::sin(azimuth), 0.0f, Math::cos(azimuth)) * distance;
		ground.y = origin.y - 20.0f;
		const Vector3 drift = Vector3(rng.random(-0.2f, 0.2f), 0.0f, rng.random(-0.2f, 0.2f)) * cloud_base_height;
		Vector3 top = ground + drift + Vector3(0.0f, cloud_base_height, 0.0f);
		position = ground;
		// Keep the bolt inside the camera's far plane: pull it in, same angles.
		const float far = camera ? camera->get_far() * 0.9f : 3500.0f;
		const float reach = MAX(top.distance_to(origin), ground.distance_to(origin));
		if (reach > far) {
			top = origin + (top - origin) * (far / reach);
			ground = origin + (ground - origin) * (far / reach);
		}
		_build_bolt(top, ground);
		// A faint stepped leader works its way down before the first stroke.
		leader_time = rng.random(0.05f, 0.1f);
		for (Vector2 &stroke : strokes) {
			stroke.x += leader_time;
		}
		direction = (top - origin).normalized();
		// Rain and haze dim distant bolts, but bright ones still show through.
		const float distance_fog = weather->get_current()->get_param(WeatherProfile::PARAM_DISTANCE_FOG);
		bolt_visibility = CLAMP(Math::exp(-distance * distance_fog * 0.05f), 0.35f, 1.0f);
	} else {
		// Inside the clouds: lights them from within, no bolt.
		const float elevation = Math::deg_to_rad(rng.random(20.0f, 60.0f));
		direction = Vector3(Math::cos(elevation) * Math::sin(azimuth), Math::sin(elevation), Math::cos(elevation) * Math::cos(azimuth));
		position = origin + direction * distance;
		bolt->set_visible(false);
	}

	light->set_basis(Basis::looking_at(-direction));
	flash_time = 0.0;
	hold_until = 0.0;
	// Near strikes light the scene more.
	const float closeness = 1.0f - Math::inverse_lerp(distance_min, MAX(distance_max, distance_min + 1.0f), distance);
	flash_strength = Math::lerp(0.6f, 1.4f, CLAMP(closeness, 0.0f, 1.0f));
	weather->emit_signal(SNAME("lightning_struck"), position, distance);
}

void WeatherLightningEffect::preview_strike() {
	if (!weather) {
		WARN_PRINT("The lightning effect isn't running: add it to a Weather node's effects.");
		return;
	}
	strike(-1.0f, 1);
	hold_until = strokes[strokes.size() - 1].x + preview_hold_time;
}

Callable WeatherLightningEffect::_get_strike_button() {
	return callable_mp(this, &WeatherLightningEffect::preview_strike);
}

void WeatherLightningEffect::update(Weather *p_weather, double p_delta) {
	WeatherEffect::update(p_weather, p_delta);
	if (!light) {
		return;
	}
	// Strikes at the weather's rate, in game only.
	if (!Engine::get_singleton()->is_editor_hint()) {
		const float per_minute = p_weather->get_current()->get_param(WeatherProfile::PARAM_LIGHTNING_PER_MINUTE);
		if (per_minute > 0.05f) {
			next_strike -= p_delta;
			if (next_strike <= 0.0) {
				strike();
				next_strike = 60.0f / per_minute * rng.random(0.4f, 1.6f);
			}
		} else {
			next_strike = rng.random(2.0f, 6.0f);
		}
	}

	// Each stroke flashes and decays fast.
	flash = 0.0f;
	if (flash_time >= 0.0) {
		flash_time += p_delta;
		for (const Vector2 &stroke : strokes) {
			if (flash_time >= stroke.x) {
				flash = MAX(flash, stroke.y * (float)Math::exp(-(flash_time - stroke.x) * 18.0));
			}
		}
		const double last = strokes.is_empty() ? 0.0 : strokes[strokes.size() - 1].x;
		if (flash_time > MAX(last + 0.8, hold_until + 0.3)) {
			flash_time = -1.0;
		}
	}
	light->set_visible(flash > 0.01f);
	light->set_color(color);
	light->set_param(Light3D::PARAM_ENERGY, flash * energy * flash_strength);
	const Ref<WeatherSkyEffect> sky = p_weather->get_effect(SNAME("WeatherSkyEffect"));
	if (sky.is_valid()) {
		sky->set_lightning(p_weather, flash, direction, color);
	}

	if (leader_time <= 0.0f || flash_time < 0.0) {
		bolt->set_visible(false);
		return;
	}
	if (flash_time < leader_time) {
		// Leader: a faint channel growing down from the cloud.
		_show_bolt(0.12f, 1.0f, flash_time / leader_time);
	} else {
		// Branches show mostly in the first stroke; later strokes light the main channel.
		const float branches = Math::exp(-(flash_time - strokes[0].x) * 10.0);
		if (flash_time < hold_until) {
			// Preview: keep it steadily lit to look at.
			_show_bolt(MAX(flash, 0.6f), MAX(branches, 0.8f), 1.0f);
		} else {
			_show_bolt(flash, branches, 1.0f);
		}
	}
}

float WeatherLightningEffect::get_flash() const {
	return flash;
}

bool WeatherLightningEffect::is_bolt_visible() const {
	return bolt && bolt->is_visible();
}

Vector3 WeatherLightningEffect::get_direction() const {
	return direction;
}

// Settings.

void WeatherLightningEffect::set_energy(float p_energy) {
	energy = MAX(0.0f, p_energy);
	emit_changed();
}

float WeatherLightningEffect::get_energy() const {
	return energy;
}

void WeatherLightningEffect::set_color(const Color &p_color) {
	color = p_color;
	emit_changed();
}

Color WeatherLightningEffect::get_color() const {
	return color;
}

void WeatherLightningEffect::set_distance_min(float p_distance) {
	distance_min = MAX(0.0f, p_distance);
	emit_changed();
}

float WeatherLightningEffect::get_distance_min() const {
	return distance_min;
}

void WeatherLightningEffect::set_distance_max(float p_distance) {
	distance_max = MAX(0.0f, p_distance);
	emit_changed();
}

float WeatherLightningEffect::get_distance_max() const {
	return distance_max;
}

void WeatherLightningEffect::set_ground_strike_chance(float p_chance) {
	ground_strike_chance = CLAMP(p_chance, 0.0f, 1.0f);
	emit_changed();
}

float WeatherLightningEffect::get_ground_strike_chance() const {
	return ground_strike_chance;
}

void WeatherLightningEffect::set_cloud_base_height(float p_height) {
	cloud_base_height = MAX(100.0f, p_height);
	emit_changed();
}

float WeatherLightningEffect::get_cloud_base_height() const {
	return cloud_base_height;
}

void WeatherLightningEffect::set_glow_color(const Color &p_color) {
	glow_color = p_color;
	if (bolt_material.is_valid()) {
		bolt_material->set_shader_parameter("glow_color", glow_color);
	}
	emit_changed();
}

Color WeatherLightningEffect::get_glow_color() const {
	return glow_color;
}

void WeatherLightningEffect::set_bolt_levels(int p_levels) {
	bolt_levels = CLAMP(p_levels, 3, 9);
	emit_changed();
}

int WeatherLightningEffect::get_bolt_levels() const {
	return bolt_levels;
}

void WeatherLightningEffect::set_bolt_roughness(float p_roughness) {
	bolt_roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	emit_changed();
}

float WeatherLightningEffect::get_bolt_roughness() const {
	return bolt_roughness;
}

void WeatherLightningEffect::set_max_branches(int p_branches) {
	max_branches = CLAMP(p_branches, 0, 12);
	emit_changed();
}

int WeatherLightningEffect::get_max_branches() const {
	return max_branches;
}

void WeatherLightningEffect::set_preview_hold_time(float p_seconds) {
	preview_hold_time = MAX(0.0f, p_seconds);
	emit_changed();
}

float WeatherLightningEffect::get_preview_hold_time() const {
	return preview_hold_time;
}

void WeatherLightningEffect::set_bolt_layers(uint32_t p_layers) {
	bolt_layers = p_layers;
	if (bolt) {
		bolt->set_layer_mask(bolt_layers);
	}
	emit_changed();
}

uint32_t WeatherLightningEffect::get_bolt_layers() const {
	return bolt_layers;
}

void WeatherLightningEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_energy", "energy"), &WeatherLightningEffect::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &WeatherLightningEffect::get_energy);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &WeatherLightningEffect::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &WeatherLightningEffect::get_color);
	ClassDB::bind_method(D_METHOD("set_distance_min", "distance"), &WeatherLightningEffect::set_distance_min);
	ClassDB::bind_method(D_METHOD("get_distance_min"), &WeatherLightningEffect::get_distance_min);
	ClassDB::bind_method(D_METHOD("set_distance_max", "distance"), &WeatherLightningEffect::set_distance_max);
	ClassDB::bind_method(D_METHOD("get_distance_max"), &WeatherLightningEffect::get_distance_max);
	ClassDB::bind_method(D_METHOD("set_ground_strike_chance", "chance"), &WeatherLightningEffect::set_ground_strike_chance);
	ClassDB::bind_method(D_METHOD("get_ground_strike_chance"), &WeatherLightningEffect::get_ground_strike_chance);
	ClassDB::bind_method(D_METHOD("set_cloud_base_height", "height"), &WeatherLightningEffect::set_cloud_base_height);
	ClassDB::bind_method(D_METHOD("get_cloud_base_height"), &WeatherLightningEffect::get_cloud_base_height);
	ClassDB::bind_method(D_METHOD("set_glow_color", "color"), &WeatherLightningEffect::set_glow_color);
	ClassDB::bind_method(D_METHOD("get_glow_color"), &WeatherLightningEffect::get_glow_color);
	ClassDB::bind_method(D_METHOD("set_bolt_levels", "levels"), &WeatherLightningEffect::set_bolt_levels);
	ClassDB::bind_method(D_METHOD("get_bolt_levels"), &WeatherLightningEffect::get_bolt_levels);
	ClassDB::bind_method(D_METHOD("set_bolt_roughness", "roughness"), &WeatherLightningEffect::set_bolt_roughness);
	ClassDB::bind_method(D_METHOD("get_bolt_roughness"), &WeatherLightningEffect::get_bolt_roughness);
	ClassDB::bind_method(D_METHOD("set_max_branches", "branches"), &WeatherLightningEffect::set_max_branches);
	ClassDB::bind_method(D_METHOD("get_max_branches"), &WeatherLightningEffect::get_max_branches);
	ClassDB::bind_method(D_METHOD("set_preview_hold_time", "seconds"), &WeatherLightningEffect::set_preview_hold_time);
	ClassDB::bind_method(D_METHOD("get_preview_hold_time"), &WeatherLightningEffect::get_preview_hold_time);
	ClassDB::bind_method(D_METHOD("set_bolt_layers", "layers"), &WeatherLightningEffect::set_bolt_layers);
	ClassDB::bind_method(D_METHOD("get_bolt_layers"), &WeatherLightningEffect::get_bolt_layers);
	ClassDB::bind_method(D_METHOD("_get_strike_button"), &WeatherLightningEffect::_get_strike_button);

	ClassDB::bind_method(D_METHOD("strike", "distance", "ground_strike"), &WeatherLightningEffect::strike, DEFVAL(-1.0f), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("preview_strike"), &WeatherLightningEffect::preview_strike);
	ClassDB::bind_method(D_METHOD("get_flash"), &WeatherLightningEffect::get_flash);
	ClassDB::bind_method(D_METHOD("is_bolt_visible"), &WeatherLightningEffect::is_bolt_visible);
	ClassDB::bind_method(D_METHOD("get_direction"), &WeatherLightningEffect::get_direction);

	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "strike_lightning", PROPERTY_HINT_TOOL_BUTTON, "Strike Lightning,Callable", PROPERTY_USAGE_EDITOR), "", "_get_strike_button");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_min", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:m"), "set_distance_min", "get_distance_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_max", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:m"), "set_distance_max", "get_distance_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_strike_chance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ground_strike_chance", "get_ground_strike_chance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_base_height", PROPERTY_HINT_RANGE, "100,5000,1,or_greater,suffix:m"), "set_cloud_base_height", "get_cloud_base_height");
	ADD_GROUP("Bolt", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "glow_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_glow_color", "get_glow_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bolt_levels", PROPERTY_HINT_RANGE, "3,9,1"), "set_bolt_levels", "get_bolt_levels");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bolt_roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_bolt_roughness", "get_bolt_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_branches", PROPERTY_HINT_RANGE, "0,12,1"), "set_max_branches", "get_max_branches");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_hold_time", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:s"), "set_preview_hold_time", "get_preview_hold_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bolt_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_bolt_layers", "get_bolt_layers");
}
