/**************************************************************************/
/*  level_stream_cell_3d.cpp                                              */
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

#include "level_stream_cell_3d.h"

#include "level_streamer_3d.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"

void LevelStreamCell3D::_update_streamer() {
	LevelStreamer3D *found = nullptr;
	if (is_inside_tree()) {
		for (Node *parent = get_parent(); parent; parent = parent->get_parent()) {
			found = Object::cast_to<LevelStreamer3D>(parent);
			if (found) {
				break;
			}
		}
	}
	if (found == streamer) {
		return;
	}
	if (streamer) {
		streamer->unregister_cell(this);
	}
	streamer = found;
	if (streamer) {
		streamer->register_cell(this);
	}
}

void LevelStreamCell3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
#ifdef TOOLS_ENABLED
			// A new cell gets a stable ID from its name, once; renaming the
			// node later keeps the ID (saved state is keyed by it).
			if (cell_id == StringName() && Engine::get_singleton()->is_editor_hint()) {
				cell_id = String(get_name()).to_snake_case();
			}
#endif
			_update_streamer();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (streamer) {
				streamer->unregister_cell(this);
				streamer = nullptr;
			}
		} break;
	}
}

void LevelStreamCell3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "unload_distance" || p_property.name == "preload_distance") {
		if (always_loaded) {
			p_property.usage |= PROPERTY_USAGE_READ_ONLY;
		}
	}
}

void LevelStreamCell3D::set_cell_id(const StringName &p_id) {
	if (cell_id == p_id) {
		return;
	}
	const StringName old = cell_id;
	cell_id = p_id;
	if (streamer) {
		streamer->cell_id_changed(this, old);
	}
	update_configuration_warnings();
}

StringName LevelStreamCell3D::get_cell_id() const {
	return cell_id;
}

void LevelStreamCell3D::set_scene_path(const String &p_path) {
	if (scene_path == p_path) {
		return;
	}
	scene_path = p_path;
	if (streamer) {
		streamer->cell_scene_changed(this);
	}
	update_configuration_warnings();
}

String LevelStreamCell3D::get_scene_path() const {
	return scene_path;
}

void LevelStreamCell3D::set_size(const Vector3 &p_size) {
	size = p_size.maxf(0.0);
	update_gizmos();
	update_configuration_warnings();
}

Vector3 LevelStreamCell3D::get_size() const {
	return size;
}

void LevelStreamCell3D::set_preload_distance(real_t p_distance) {
	preload_distance = MAX(p_distance, (real_t)0.0);
	update_gizmos();
	update_configuration_warnings();
}

real_t LevelStreamCell3D::get_preload_distance() const {
	return preload_distance;
}

void LevelStreamCell3D::set_unload_distance(real_t p_distance) {
	unload_distance = MAX(p_distance, (real_t)0.0);
	update_gizmos();
	update_configuration_warnings();
}

real_t LevelStreamCell3D::get_unload_distance() const {
	return unload_distance;
}

void LevelStreamCell3D::set_neighbors(const PackedStringArray &p_neighbors) {
	neighbors = p_neighbors;
	update_configuration_warnings();
}

PackedStringArray LevelStreamCell3D::get_neighbors() const {
	return neighbors;
}

void LevelStreamCell3D::set_always_loaded(bool p_always) {
	always_loaded = p_always;
	notify_property_list_changed();
}

bool LevelStreamCell3D::is_always_loaded() const {
	return always_loaded;
}

AABB LevelStreamCell3D::get_local_bounds() const {
	return AABB(-size * 0.5, size);
}

real_t LevelStreamCell3D::distance_to(const Vector3 &p_global_point) const {
	// Measured in the cell's space, so rotated cells work; scale is assumed
	// uniform (the local distance is multiplied back by it).
	const Transform3D xform = get_global_transform();
	const Vector3 local = xform.affine_inverse().xform(p_global_point);
	const Vector3 half = size * 0.5;
	const Vector3 outside = (local.abs() - half).maxf(0.0);
	return outside.length() * xform.basis.get_scale().x;
}

bool LevelStreamCell3D::contains(const Vector3 &p_global_point) const {
	return distance_to(p_global_point) <= 0.0;
}

Node *LevelStreamCell3D::get_content() const {
	return streamer ? streamer->get_cell_content(cell_id) : nullptr;
}

int LevelStreamCell3D::get_state() const {
	return streamer ? streamer->get_cell_state(cell_id) : LevelStreamer3D::CELL_UNLOADED;
}

PackedStringArray LevelStreamCell3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!streamer) {
		warnings.push_back(RTR("A LevelStreamCell3D only streams under a LevelStreamer3D. Make it a descendant of one."));
	}
	if (cell_id == StringName()) {
		warnings.push_back(RTR("The cell needs a cell ID: saved state and neighbors refer to it."));
	} else if (streamer && streamer->is_duplicate_cell(this)) {
		warnings.push_back(vformat(RTR("Another cell under this streamer already uses the ID \"%s\". Cell IDs must be unique."), cell_id));
	}
	if (scene_path.is_empty()) {
		warnings.push_back(RTR("No scene is set: the cell streams nothing."));
	} else if (!ResourceLoader::exists(scene_path, "PackedScene")) {
		warnings.push_back(vformat(RTR("The scene \"%s\" does not exist or is not a scene."), scene_path));
	}
	if (!always_loaded && unload_distance < preload_distance) {
		warnings.push_back(RTR("The unload distance must be at least the preload distance, or the cell loads and unloads over and over at its boundary."));
	}
	if (size.x <= 0.0 || size.y <= 0.0 || size.z <= 0.0) {
		warnings.push_back(RTR("The cell's size must be greater than zero on every axis."));
	}
	if (streamer) {
		for (const String &neighbor : neighbors) {
			if (!streamer->has_cell(neighbor)) {
				warnings.push_back(vformat(RTR("The neighbor \"%s\" is not a cell of this streamer."), neighbor));
			} else if (StringName(neighbor) == cell_id) {
				warnings.push_back(RTR("A cell can't be its own neighbor."));
			}
		}
	}
	return warnings;
}

void LevelStreamCell3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_id", "id"), &LevelStreamCell3D::set_cell_id);
	ClassDB::bind_method(D_METHOD("get_cell_id"), &LevelStreamCell3D::get_cell_id);
	ClassDB::bind_method(D_METHOD("set_scene_path", "path"), &LevelStreamCell3D::set_scene_path);
	ClassDB::bind_method(D_METHOD("get_scene_path"), &LevelStreamCell3D::get_scene_path);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &LevelStreamCell3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &LevelStreamCell3D::get_size);
	ClassDB::bind_method(D_METHOD("set_preload_distance", "distance"), &LevelStreamCell3D::set_preload_distance);
	ClassDB::bind_method(D_METHOD("get_preload_distance"), &LevelStreamCell3D::get_preload_distance);
	ClassDB::bind_method(D_METHOD("set_unload_distance", "distance"), &LevelStreamCell3D::set_unload_distance);
	ClassDB::bind_method(D_METHOD("get_unload_distance"), &LevelStreamCell3D::get_unload_distance);
	ClassDB::bind_method(D_METHOD("set_neighbors", "neighbors"), &LevelStreamCell3D::set_neighbors);
	ClassDB::bind_method(D_METHOD("get_neighbors"), &LevelStreamCell3D::get_neighbors);
	ClassDB::bind_method(D_METHOD("set_always_loaded", "always"), &LevelStreamCell3D::set_always_loaded);
	ClassDB::bind_method(D_METHOD("is_always_loaded"), &LevelStreamCell3D::is_always_loaded);
	ClassDB::bind_method(D_METHOD("distance_to", "global_point"), &LevelStreamCell3D::distance_to);
	ClassDB::bind_method(D_METHOD("contains", "global_point"), &LevelStreamCell3D::contains);
	ClassDB::bind_method(D_METHOD("get_content"), &LevelStreamCell3D::get_content);
	ClassDB::bind_method(D_METHOD("get_state"), &LevelStreamCell3D::get_state);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "cell_id"), "set_cell_id", "get_cell_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scene_path", PROPERTY_HINT_FILE, "*.tscn,*.scn"), "set_scene_path", "get_scene_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_RANGE, "0.01,4096,0.01,or_greater,suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preload_distance", PROPERTY_HINT_RANGE, "0,4096,0.1,or_greater,suffix:m"), "set_preload_distance", "get_preload_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unload_distance", PROPERTY_HINT_RANGE, "0,4096,0.1,or_greater,suffix:m"), "set_unload_distance", "get_unload_distance");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "neighbors"), "set_neighbors", "get_neighbors");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "always_loaded"), "set_always_loaded", "is_always_loaded");
}
