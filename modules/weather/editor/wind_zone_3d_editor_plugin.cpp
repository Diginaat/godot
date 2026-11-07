/**************************************************************************/
/*  wind_zone_3d_editor_plugin.cpp                                        */
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

#include "wind_zone_3d_editor_plugin.h"

#include "../wind/wind_zone_3d.h"

#include "editor/scene/3d/node_3d_editor_plugin.h"

static void _add_arrow(Vector<Vector3> &r_lines, const Vector3 &p_from, const Vector3 &p_to) {
	r_lines.push_back(p_from);
	r_lines.push_back(p_to);
	const Vector3 along = p_to - p_from;
	const float length = along.length();
	if (length < 0.0001f) {
		return;
	}
	const Vector3 direction = along / length;
	const Vector3 reference = Math::abs(direction.y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
	const Vector3 side = direction.cross(reference).normalized() * length * 0.15f;
	const Vector3 back = p_to - direction * length * 0.25f;
	r_lines.push_back(p_to);
	r_lines.push_back(back + side);
	r_lines.push_back(p_to);
	r_lines.push_back(back - side);
}

static void _add_circle(Vector<Vector3> &r_lines, float p_radius, int p_axis) {
	const int segments = 64;
	for (int i = 0; i < segments; i++) {
		const float a = Math::TAU * i / segments;
		const float b = Math::TAU * (i + 1) / segments;
		Vector3 from;
		Vector3 to;
		from[(p_axis + 1) % 3] = Math::cos(a) * p_radius;
		from[(p_axis + 2) % 3] = Math::sin(a) * p_radius;
		to[(p_axis + 1) % 3] = Math::cos(b) * p_radius;
		to[(p_axis + 2) % 3] = Math::sin(b) * p_radius;
		r_lines.push_back(from);
		r_lines.push_back(to);
	}
}

WindZone3DGizmoPlugin::WindZone3DGizmoPlugin() {
	create_material("wind_zone", Color(0.55f, 0.82f, 1.0f));
	create_material("wind_zone_disabled", Color(0.6f, 0.6f, 0.6f, 0.5f));
}

bool WindZone3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<WindZone3D>(p_spatial) != nullptr;
}

String WindZone3DGizmoPlugin::get_gizmo_name() const {
	return "WindZone3D";
}

int WindZone3DGizmoPlugin::get_priority() const {
	return -1;
}

void WindZone3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	const WindZone3D *zone = Object::cast_to<WindZone3D>(p_gizmo->get_node_3d());
	if (!zone) {
		return;
	}
	Vector<Vector3> lines;
	Vector3 extent;
	if (zone->get_shape() == WindZone3D::SHAPE_SPHERE) {
		const float radius = zone->get_radius();
		extent = Vector3(radius, radius, radius);
		for (int axis = 0; axis < 3; axis++) {
			_add_circle(lines, radius, axis);
		}
	} else {
		extent = zone->get_size() * 0.5f;
		const AABB box(-extent, extent * 2.0f);
		for (int i = 0; i < 12; i++) {
			Vector3 from;
			Vector3 to;
			box.get_edge(i, from, to);
			lines.push_back(from);
			lines.push_back(to);
		}
	}

	// How the air moves; a negative strength turns the arrows around.
	const float sign = zone->get_strength() < 0.0f ? -1.0f : 1.0f;
	switch (zone->get_mode()) {
		case WindZone3D::MODE_DIRECTIONAL: {
			for (const Vector2 &spot : { Vector2(0.0f, 0.0f), Vector2(0.4f, 0.0f), Vector2(-0.4f, 0.0f), Vector2(0.0f, 0.4f), Vector2(0.0f, -0.4f) }) {
				const Vector3 base(spot.x * extent.x, spot.y * extent.y, 0.0f);
				_add_arrow(lines, base + Vector3(0.0f, 0.0f, 0.6f * extent.z * sign), base - Vector3(0.0f, 0.0f, 0.6f * extent.z * sign));
			}
		} break;
		case WindZone3D::MODE_RADIAL: {
			for (int axis = 0; axis < 3; axis++) {
				for (const float side : { -1.0f, 1.0f }) {
					Vector3 inner;
					Vector3 outer;
					inner[axis] = side * extent[axis] * (sign > 0.0f ? 0.2f : 0.8f);
					outer[axis] = side * extent[axis] * (sign > 0.0f ? 0.8f : 0.2f);
					_add_arrow(lines, inner, outer);
				}
			}
		} break;
		case WindZone3D::MODE_VORTEX: {
			const float r = 0.6f * MIN(extent.x, extent.z);
			_add_circle(lines, r, 1);
			for (int i = 0; i < 4; i++) {
				const float a = Math::TAU * i / 4.0f;
				const Vector3 at(Math::cos(a) * r, 0.0f, Math::sin(a) * r);
				// The wind turns as cross(+Y, offset).
				const Vector3 tangent = Vector3(0.0f, 1.0f, 0.0f).cross(at).normalized() * r * 0.35f * sign;
				_add_arrow(lines, at - tangent, at + tangent);
			}
		} break;
	}
	p_gizmo->add_lines(lines, get_material(zone->is_enabled() ? "wind_zone" : "wind_zone_disabled", p_gizmo));
}

WindZone3DEditorPlugin::WindZone3DEditorPlugin() {
	gizmo_plugin.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(gizmo_plugin);
}
