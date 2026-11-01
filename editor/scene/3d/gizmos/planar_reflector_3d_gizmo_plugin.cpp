/**************************************************************************/
/*  planar_reflector_3d_gizmo_plugin.cpp                                  */
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

#include "planar_reflector_3d_gizmo_plugin.h"

#include "editor/settings/editor_settings.h"
#include "scene/3d/planar_reflector_3d.h"

PlanarReflector3DGizmoPlugin::PlanarReflector3DGizmoPlugin() {
	Color gizmo_color = EDITOR_GET("editors/3d_gizmos/gizmo_colors/reflection_probe");
	create_material("reflector_material", gizmo_color);
	gizmo_color.a = 0.1;
	create_material("reflector_material_internal", gizmo_color);
}

bool PlanarReflector3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<PlanarReflector3D>(p_spatial) != nullptr;
}

String PlanarReflector3DGizmoPlugin::get_gizmo_name() const {
	return "PlanarReflector3D";
}

int PlanarReflector3DGizmoPlugin::get_priority() const {
	return -1;
}

void PlanarReflector3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	PlanarReflector3D *reflector = Object::cast_to<PlanarReflector3D>(p_gizmo->get_node_3d());

	p_gizmo->clear();

	Vector2 half = reflector->get_size() * 0.5;
	Vector3 corners[4] = {
		Vector3(-half.x, 0, -half.y),
		Vector3(half.x, 0, -half.y),
		Vector3(half.x, 0, half.y),
		Vector3(-half.x, 0, half.y),
	};

	// Outline, diagonals and the normal (the reflected side).
	Vector<Vector3> lines;
	for (int i = 0; i < 4; i++) {
		lines.push_back(corners[i]);
		lines.push_back(corners[(i + 1) % 4]);
	}
	lines.push_back(corners[0]);
	lines.push_back(corners[2]);
	lines.push_back(corners[1]);
	lines.push_back(corners[3]);

	float arrow = MIN(MAX(MIN(half.x, half.y) * 0.5, 0.1), 1.0);
	lines.push_back(Vector3());
	lines.push_back(Vector3(0, arrow, 0));
	lines.push_back(Vector3(0, arrow, 0));
	lines.push_back(Vector3(arrow * 0.2, arrow * 0.8, 0));
	lines.push_back(Vector3(0, arrow, 0));
	lines.push_back(Vector3(-arrow * 0.2, arrow * 0.8, 0));

	p_gizmo->add_lines(lines, get_material("reflector_material", p_gizmo));
	p_gizmo->add_collision_segments(lines);

	Vector<Vector3> triangles = { corners[0], corners[1], corners[2], corners[0], corners[2], corners[3] };
	Ref<TriangleMesh> mesh;
	mesh.instantiate();
	mesh->create(triangles);
	p_gizmo->add_collision_triangles(mesh);
}
