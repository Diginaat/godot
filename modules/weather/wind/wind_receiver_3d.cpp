/**************************************************************************/
/*  wind_receiver_3d.cpp                                                  */
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

#include "wind_receiver_3d.h"

#include "wind_server.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/shape_3d.h"

RigidBody3D *WindReceiver3D::_get_body() const {
	return Object::cast_to<RigidBody3D>(get_parent());
}

void WindReceiver3D::_update_shape_size() {
	shape_size_dirty = false;
	shape_size = Vector3();
	const Node3D *body = Object::cast_to<Node3D>(get_parent());
	if (!body) {
		return;
	}
	AABB bounds;
	bool first = true;
	const Transform3D to_body = body->get_global_transform().affine_inverse();
	for (int i = 0; i < body->get_child_count(); i++) {
		const CollisionShape3D *shape_node = Object::cast_to<CollisionShape3D>(body->get_child(i));
		if (!shape_node || shape_node->is_disabled() || shape_node->get_shape().is_null()) {
			continue;
		}
		const Transform3D xform = to_body * shape_node->get_global_transform();
		const Vector<Vector3> lines = shape_node->get_shape()->get_debug_mesh_lines();
		for (const Vector3 &point : lines) {
			const Vector3 p = xform.xform(point);
			if (first) {
				bounds = AABB(p, Vector3());
				first = false;
			} else {
				bounds.expand_to(p);
			}
		}
	}
	shape_size = bounds.size;
}

float WindReceiver3D::_get_area_facing(const Vector3 &p_direction) const {
	if (area > 0.0f) {
		return area;
	}
	const Node3D *body = Object::cast_to<Node3D>(get_parent());
	if (!body) {
		return 0.0f;
	}
	// The body's box seen along the wind: each face counts by how much it faces it.
	const Basis basis = body->get_global_transform().basis;
	const Vector3 x = basis.get_column(0);
	const Vector3 y = basis.get_column(1);
	const Vector3 z = basis.get_column(2);
	const Vector3 size = shape_size * Vector3(x.length(), y.length(), z.length());
	return Math::abs(p_direction.dot(x.normalized())) * size.y * size.z +
			Math::abs(p_direction.dot(y.normalized())) * size.x * size.z +
			Math::abs(p_direction.dot(z.normalized())) * size.x * size.y;
}

void WindReceiver3D::_physics_step(double p_delta) {
	wind_force = Vector3();
	wind_velocity = Vector3();
	const WindServer *server = WindServer::get_singleton();
	if (!enabled || !server || p_delta <= 0.0) {
		return;
	}
	if (shape_size_dirty) {
		_update_shape_size();
	}
	const Vector3 position = get_global_position();
	wind_velocity = server->sample(position);

	RigidBody3D *body = _get_body();
	Vector3 relative = wind_velocity;
	Vector3 offset;
	if (body) {
		offset = position - body->get_global_position();
		relative -= body->get_linear_velocity() + body->get_angular_velocity().cross(offset);
	}
	const float speed = relative.length();
	if (speed < 0.001f) {
		return;
	}
	const Vector3 direction = relative / speed;
	float force = 0.5f * server->get_air_density() * drag_coefficient * _get_area_facing(direction) * speed * speed * strength;
	if (!body) {
		wind_force = direction * force;
		return;
	}
	const float mass = MAX((float)body->get_mass(), 0.001f);
	// Drag can at most bring the body to the wind's speed in one step;
	// more would overshoot and shake light, wide bodies.
	force = MIN(force, mass * speed / (float)p_delta);
	wind_force = direction * force;
	if (body->is_sleeping() && force / mass < wake_acceleration) {
		return;
	}
	body->apply_force(wind_force, offset);
}

void WindReceiver3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_PARENTED: {
			shape_size_dirty = true;
			update_configuration_warnings();
		} break;
		case NOTIFICATION_READY: {
			set_physics_process_internal(!Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			_physics_step(get_physics_process_delta_time());
		} break;
	}
}

void WindReceiver3D::update_shape_size() {
	_update_shape_size();
}

void WindReceiver3D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
}

bool WindReceiver3D::is_enabled() const {
	return enabled;
}

void WindReceiver3D::set_drag_coefficient(float p_coefficient) {
	drag_coefficient = MAX(0.0f, p_coefficient);
}

float WindReceiver3D::get_drag_coefficient() const {
	return drag_coefficient;
}

void WindReceiver3D::set_area(float p_area) {
	area = MAX(0.0f, p_area);
}

float WindReceiver3D::get_area() const {
	return area;
}

void WindReceiver3D::set_strength(float p_strength) {
	strength = MAX(0.0f, p_strength);
}

float WindReceiver3D::get_strength() const {
	return strength;
}

void WindReceiver3D::set_wake_acceleration(float p_acceleration) {
	wake_acceleration = MAX(0.0f, p_acceleration);
}

float WindReceiver3D::get_wake_acceleration() const {
	return wake_acceleration;
}

Vector3 WindReceiver3D::get_wind_force() const {
	return wind_force;
}

Vector3 WindReceiver3D::get_wind_velocity() const {
	return wind_velocity;
}

float WindReceiver3D::get_effective_area() const {
	const float speed = wind_velocity.length();
	return _get_area_facing(speed > 0.001f ? wind_velocity / speed : Vector3(0.0f, 0.0f, -1.0f));
}

PackedStringArray WindReceiver3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!_get_body()) {
		warnings.push_back(RTR("WindReceiver3D only pushes a RigidBody3D parent. Under other nodes it just computes get_wind_force()."));
	}
	return warnings;
}

void WindReceiver3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &WindReceiver3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &WindReceiver3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_drag_coefficient", "coefficient"), &WindReceiver3D::set_drag_coefficient);
	ClassDB::bind_method(D_METHOD("get_drag_coefficient"), &WindReceiver3D::get_drag_coefficient);
	ClassDB::bind_method(D_METHOD("set_area", "area"), &WindReceiver3D::set_area);
	ClassDB::bind_method(D_METHOD("get_area"), &WindReceiver3D::get_area);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &WindReceiver3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &WindReceiver3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_wake_acceleration", "acceleration"), &WindReceiver3D::set_wake_acceleration);
	ClassDB::bind_method(D_METHOD("get_wake_acceleration"), &WindReceiver3D::get_wake_acceleration);
	ClassDB::bind_method(D_METHOD("get_wind_force"), &WindReceiver3D::get_wind_force);
	ClassDB::bind_method(D_METHOD("get_wind_velocity"), &WindReceiver3D::get_wind_velocity);
	ClassDB::bind_method(D_METHOD("get_effective_area"), &WindReceiver3D::get_effective_area);
	ClassDB::bind_method(D_METHOD("update_shape_size"), &WindReceiver3D::update_shape_size);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag_coefficient", PROPERTY_HINT_RANGE, "0,3,0.01"), "set_drag_coefficient", "get_drag_coefficient");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "area", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m²"), "set_area", "get_area");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wake_acceleration", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:m/s²"), "set_wake_acceleration", "get_wake_acceleration");
}

WindReceiver3D::WindReceiver3D() {
}
