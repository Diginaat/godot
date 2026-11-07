/**************************************************************************/
/*  wind_zone_3d.cpp                                                      */
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

#include "wind_zone_3d.h"

#include "wind_server.h"

#include "core/object/class_db.h"

static WindServer::Zone _make_zone(const Transform3D &p_transform, WindZone3D::Mode p_mode, WindZone3D::Shape p_shape, float p_radius, const Vector3 &p_size, float p_strength, float p_falloff, bool p_replace) {
	WindServer::Zone zone;
	zone.origin = p_transform.origin;
	// The node's scale stretches the shape too.
	const Vector3 extent = p_shape == WindZone3D::SHAPE_SPHERE ? Vector3(p_radius, p_radius, p_radius) : p_size * 0.5f;
	zone.to_unit = p_transform.basis.scaled_local(extent.maxf(0.001f)).inverse();
	// Directional zones blow along -Z (like a camera looks); vortices turn
	// around +Y.
	zone.axis = p_mode == WindZone3D::MODE_VORTEX ? p_transform.basis.get_column(1).normalized() : -p_transform.basis.get_column(2).normalized();
	zone.strength = p_strength;
	zone.falloff = p_falloff;
	zone.mode = (WindServer::ZoneMode)p_mode;
	zone.shape = (WindServer::ZoneShape)p_shape;
	zone.replace = p_replace;
	return zone;
}

void WindZone3D::_update_zone() {
	WindServer *server = WindServer::get_singleton();
	if (!server) {
		return;
	}
	if (!is_inside_tree() || !enabled) {
		if (registered) {
			server->remove_zone(this);
			registered = false;
		}
		return;
	}
	server->set_zone(this, _make_zone(get_global_transform(), mode, shape, radius, size, strength, falloff, replace_global));
	registered = true;
}

void WindZone3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_notify_transform(true);
			_update_zone();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_update_zone();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_update_zone();
		} break;
	}
}

Vector3 WindZone3D::get_wind_at(const Vector3 &p_position) const {
	if (!is_inside_tree()) {
		return Vector3();
	}
	return WindServer::apply_zone(_make_zone(get_global_transform(), mode, shape, radius, size, strength, falloff, replace_global), p_position, Vector3());
}

bool WindZone3D::is_inside(const Vector3 &p_position) const {
	if (!is_inside_tree()) {
		return false;
	}
	const WindServer::Zone zone = _make_zone(get_global_transform(), mode, shape, radius, size, strength, falloff, replace_global);
	const Vector3 unit = zone.to_unit.xform(p_position - zone.origin);
	return shape == SHAPE_SPHERE ? unit.length() < 1.0f : MAX(MAX(Math::abs(unit.x), Math::abs(unit.y)), Math::abs(unit.z)) < 1.0f;
}

WindZone3D::~WindZone3D() {
	if (registered && WindServer::get_singleton()) {
		WindServer::get_singleton()->remove_zone(this);
	}
}

// Settings.

void WindZone3D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	_update_zone();
	update_gizmos();
}

bool WindZone3D::is_enabled() const {
	return enabled;
}

void WindZone3D::set_mode(Mode p_mode) {
	mode = p_mode;
	_update_zone();
	update_gizmos();
}

WindZone3D::Mode WindZone3D::get_mode() const {
	return mode;
}

void WindZone3D::set_shape(Shape p_shape) {
	shape = p_shape;
	_update_zone();
	update_gizmos();
	notify_property_list_changed();
}

WindZone3D::Shape WindZone3D::get_shape() const {
	return shape;
}

void WindZone3D::set_radius(float p_radius) {
	radius = MAX(0.01f, p_radius);
	_update_zone();
	update_gizmos();
}

float WindZone3D::get_radius() const {
	return radius;
}

void WindZone3D::set_size(const Vector3 &p_size) {
	size = p_size.maxf(0.01f);
	_update_zone();
	update_gizmos();
}

Vector3 WindZone3D::get_size() const {
	return size;
}

void WindZone3D::set_strength(float p_strength) {
	strength = p_strength;
	_update_zone();
	// The arrows turn around for a negative strength.
	update_gizmos();
}

float WindZone3D::get_strength() const {
	return strength;
}

void WindZone3D::set_falloff(float p_falloff) {
	falloff = CLAMP(p_falloff, 0.0f, 1.0f);
	_update_zone();
}

float WindZone3D::get_falloff() const {
	return falloff;
}

void WindZone3D::set_replace_global(bool p_replace) {
	replace_global = p_replace;
	_update_zone();
}

bool WindZone3D::is_replacing_global() const {
	return replace_global;
}

void WindZone3D::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "radius" && shape != SHAPE_SPHERE) || (p_property.name == "size" && shape != SHAPE_BOX)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void WindZone3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &WindZone3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &WindZone3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &WindZone3D::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &WindZone3D::get_mode);
	ClassDB::bind_method(D_METHOD("set_shape", "shape"), &WindZone3D::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &WindZone3D::get_shape);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &WindZone3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &WindZone3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &WindZone3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &WindZone3D::get_size);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &WindZone3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &WindZone3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_falloff", "falloff"), &WindZone3D::set_falloff);
	ClassDB::bind_method(D_METHOD("get_falloff"), &WindZone3D::get_falloff);
	ClassDB::bind_method(D_METHOD("set_replace_global", "replace"), &WindZone3D::set_replace_global);
	ClassDB::bind_method(D_METHOD("is_replacing_global"), &WindZone3D::is_replacing_global);
	ClassDB::bind_method(D_METHOD("get_wind_at", "position"), &WindZone3D::get_wind_at);
	ClassDB::bind_method(D_METHOD("is_inside", "position"), &WindZone3D::is_inside);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Directional,Radial,Vortex"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Sphere,Box"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,100,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "-100,100,0.1,or_greater,or_less,suffix:m/s"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "falloff", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_falloff", "get_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "replace_global"), "set_replace_global", "is_replacing_global");

	BIND_ENUM_CONSTANT(MODE_DIRECTIONAL);
	BIND_ENUM_CONSTANT(MODE_RADIAL);
	BIND_ENUM_CONSTANT(MODE_VORTEX);
	BIND_ENUM_CONSTANT(SHAPE_SPHERE);
	BIND_ENUM_CONSTANT(SHAPE_BOX);
}
