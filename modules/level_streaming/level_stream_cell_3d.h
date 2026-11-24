/**************************************************************************/
/*  level_stream_cell_3d.h                                                */
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

#include "scene/3d/node_3d.h"

class LevelStreamer3D;

// One streamed part of a level: an ordinary scene file, loaded and added as
// this node's child while the streamer's target is near the cell's bounds.
// Placed in the editor under a LevelStreamer3D like any other node.
class LevelStreamCell3D : public Node3D {
	GDCLASS(LevelStreamCell3D, Node3D);

	StringName cell_id;
	String scene_path;
	Vector3 size = Vector3(20, 10, 20);
	real_t preload_distance = 30.0;
	real_t unload_distance = 45.0;
	PackedStringArray neighbors;
	bool always_loaded = false;

	LevelStreamer3D *streamer = nullptr;

	void _update_streamer();

protected:
	void _notification(int p_what);
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_cell_id(const StringName &p_id);
	StringName get_cell_id() const;

	void set_scene_path(const String &p_path);
	String get_scene_path() const;

	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_preload_distance(real_t p_distance);
	real_t get_preload_distance() const;

	void set_unload_distance(real_t p_distance);
	real_t get_unload_distance() const;

	void set_neighbors(const PackedStringArray &p_neighbors);
	PackedStringArray get_neighbors() const;

	void set_always_loaded(bool p_always);
	bool is_always_loaded() const;

	// Bounds in this node's local space (centered on its origin).
	AABB get_local_bounds() const;
	// Distance from a global point to the transformed bounds (0 inside).
	real_t distance_to(const Vector3 &p_global_point) const;
	bool contains(const Vector3 &p_global_point) const;

	// The streamed scene's root while the cell is active, else null.
	Node *get_content() const;
	int get_state() const; // LevelStreamer3D::CellState.

	LevelStreamer3D *get_streamer() const { return streamer; }

	PackedStringArray get_configuration_warnings() const override;
};
