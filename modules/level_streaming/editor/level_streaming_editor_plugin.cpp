/**************************************************************************/
/*  level_streaming_editor_plugin.cpp                                     */
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

#include "level_streaming_editor_plugin.h"

#include "../level_stream_cell_3d.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "scene/resources/packed_scene.h"

static void _add_box(Vector<Vector3> &r_lines, const AABB &p_box) {
	for (int i = 0; i < 12; i++) {
		Vector3 from;
		Vector3 to;
		p_box.get_edge(i, from, to);
		r_lines.push_back(from);
		r_lines.push_back(to);
	}
}

LevelStreamCell3DGizmoPlugin::LevelStreamCell3DGizmoPlugin() {
	create_material("cell", Color(1.0f, 0.75f, 0.3f));
	create_material("cell_preload", Color(1.0f, 0.75f, 0.3f, 0.35f));
	create_material("cell_unload", Color(1.0f, 0.4f, 0.3f, 0.2f));
}

bool LevelStreamCell3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<LevelStreamCell3D>(p_spatial) != nullptr;
}

String LevelStreamCell3DGizmoPlugin::get_gizmo_name() const {
	return "LevelStreamCell3D";
}

int LevelStreamCell3DGizmoPlugin::get_priority() const {
	return -1;
}

void LevelStreamCell3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	const LevelStreamCell3D *cell = Object::cast_to<LevelStreamCell3D>(p_gizmo->get_node_3d());
	if (!cell) {
		return;
	}
	const AABB bounds = cell->get_local_bounds();
	Vector<Vector3> lines;
	_add_box(lines, bounds);
	p_gizmo->add_lines(lines, get_material("cell", p_gizmo));
	if (cell->is_always_loaded()) {
		return;
	}
	// Boxes grown by the distances (the real regions have rounded corners).
	lines.clear();
	_add_box(lines, bounds.grow(cell->get_preload_distance()));
	p_gizmo->add_lines(lines, get_material("cell_preload", p_gizmo));
	lines.clear();
	_add_box(lines, bounds.grow(cell->get_unload_distance()));
	p_gizmo->add_lines(lines, get_material("cell_unload", p_gizmo));
}

static void _add_scene_path(Vector<String> &r_paths, const String &p_value) {
	const String path = ResourceUID::ensure_path(p_value);
	if (!path.is_empty() && ResourceLoader::exists(path, "PackedScene")) {
		r_paths.push_back(path);
	}
}

Vector<String> LevelStreamingExportPlugin::get_runtime_dependencies(const String &p_path) const {
	Vector<String> paths;
	const String extension = p_path.get_extension().to_lower();
	if (extension == "tscn") {
		// Read the text instead of loading the scene, which would load all
		// of its resources: look for the cells' scene_path values.
		const String text = FileAccess::get_file_as_string(p_path);
		if (!text.contains("scene_path = \"")) {
			return paths;
		}
		for (const String &line : text.split("\n")) {
			if (line.begins_with("scene_path = \"")) {
				_add_scene_path(paths, line.substr(14).get_slicec('"', 0));
			}
		}
	} else if (extension == "scn") {
		Ref<PackedScene> scene = ResourceLoader::load(p_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE);
		if (scene.is_null()) {
			return paths;
		}
		const Ref<SceneState> state = scene->get_state();
		for (int node = 0; node < state->get_node_count(); node++) {
			for (int property = 0; property < state->get_node_property_count(node); property++) {
				if (state->get_node_property_name(node, property) == SNAME("scene_path")) {
					_add_scene_path(paths, state->get_node_property_value(node, property));
				}
			}
		}
	}
	return paths;
}

LevelStreamingEditorPlugin::LevelStreamingEditorPlugin() {
	gizmo_plugin.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(gizmo_plugin);
}
