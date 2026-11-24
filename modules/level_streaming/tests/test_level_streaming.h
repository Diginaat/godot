/**************************************************************************/
/*  test_level_streaming.h                                                */
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

#include "../level_stream_cell_3d.h"
#include "../level_streamer_3d.h"

#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestLevelStreaming {

static bool _warns(const Node *p_node, const String &p_fragment) {
	for (const String &warning : p_node->get_configuration_warnings()) {
		if (warning.contains(p_fragment)) {
			return true;
		}
	}
	return false;
}

static LevelStreamCell3D *_cell(const StringName &p_id, const String &p_scene = String()) {
	LevelStreamCell3D *cell = memnew(LevelStreamCell3D);
	cell->set_cell_id(p_id);
	cell->set_scene_path(p_scene);
	return cell;
}

TEST_CASE("[SceneTree][LevelStreaming] Cell registration and IDs") {
	LevelStreamer3D *streamer = memnew(LevelStreamer3D);
	streamer->set_active(false);
	SceneTree::get_singleton()->get_root()->add_child(streamer);

	LevelStreamCell3D *a = _cell("a");
	LevelStreamCell3D *b = _cell("b");
	streamer->add_child(a);
	// Cells may be deeper than direct children.
	Node3D *group = memnew(Node3D);
	streamer->add_child(group);
	group->add_child(b);
	CHECK(streamer->has_cell("a"));
	CHECK(streamer->has_cell("b"));
	CHECK(streamer->get_cell("b") == b);
	CHECK(streamer->get_cell_ids().size() == 2);

	SUBCASE("A duplicate ID is rejected and takes over when the first cell leaves") {
		ERR_PRINT_OFF;
		LevelStreamCell3D *duplicate = _cell("a");
		streamer->add_child(duplicate);
		ERR_PRINT_ON;
		CHECK(streamer->get_cell("a") == a);
		CHECK(streamer->is_duplicate_cell(duplicate));
		CHECK(_warns(duplicate, "already uses the ID"));
		CHECK(_warns(streamer, "not streamed"));

		memdelete(a);
		CHECK(streamer->get_cell("a") == duplicate);
		CHECK_FALSE(streamer->is_duplicate_cell(duplicate));
		CHECK_FALSE(_warns(streamer, "not streamed"));
	}

	SUBCASE("Changing the ID re-registers the cell") {
		a->set_cell_id("c");
		CHECK_FALSE(streamer->has_cell("a"));
		CHECK(streamer->get_cell("c") == a);
	}

	SUBCASE("A cell outside the tree is not streamed") {
		group->remove_child(b);
		CHECK_FALSE(streamer->has_cell("b"));
		CHECK(_warns(b, "only streams under a LevelStreamer3D"));
		memdelete(b);
	}

	memdelete(streamer);
}

TEST_CASE("[SceneTree][LevelStreaming] Configuration warnings") {
	LevelStreamer3D *streamer = memnew(LevelStreamer3D);
	streamer->set_active(false);
	SceneTree::get_singleton()->get_root()->add_child(streamer);
	CHECK(_warns(streamer, "Add LevelStreamCell3D nodes"));

	LevelStreamCell3D *cell = _cell("cell", "res://does_not_exist.tscn");
	streamer->add_child(cell);
	CHECK_FALSE(_warns(streamer, "Add LevelStreamCell3D nodes"));
	CHECK(_warns(cell, "does not exist"));

	cell->set_scene_path(String());
	CHECK(_warns(cell, "No scene is set"));

	CHECK_FALSE(_warns(cell, "unload distance must be"));
	cell->set_preload_distance(50.0);
	cell->set_unload_distance(10.0);
	CHECK(_warns(cell, "unload distance must be"));
	cell->set_always_loaded(true);
	CHECK_FALSE(_warns(cell, "unload distance must be"));

	cell->set_size(Vector3(0, 10, 10));
	CHECK(_warns(cell, "size must be greater"));

	cell->set_neighbors({ "nowhere", "cell" });
	CHECK(_warns(cell, "\"nowhere\" is not a cell"));
	CHECK(_warns(cell, "its own neighbor"));

	LevelStreamCell3D *unnamed = _cell(StringName());
	streamer->add_child(unnamed);
	CHECK(_warns(unnamed, "needs a cell ID"));
	CHECK(_warns(streamer, "not streamed"));

	Node *not_3d = memnew(Node);
	not_3d->set_name("NotSpatial");
	SceneTree::get_singleton()->get_root()->add_child(not_3d);
	streamer->set_target_path(NodePath("../Missing"));
	CHECK(_warns(streamer, "target node does not exist"));
	streamer->set_target_path(NodePath("../NotSpatial"));
	CHECK(_warns(streamer, "must be a Node3D"));

	memdelete(not_3d);
	memdelete(streamer);
}

TEST_CASE("[SceneTree][LevelStreaming] Cell distance") {
	LevelStreamCell3D *cell = memnew(LevelStreamCell3D);
	// Distances use the global transform.
	SceneTree::get_singleton()->get_root()->add_child(cell);
	cell->set_size(Vector3(20, 10, 20));
	CHECK(cell->distance_to(Vector3(0, 0, 0)) == doctest::Approx(0.0));
	CHECK(cell->contains(Vector3(9, 4, -9)));
	CHECK(cell->distance_to(Vector3(13, 0, 0)) == doctest::Approx(3.0));
	CHECK(cell->distance_to(Vector3(13, 0, 14)) == doctest::Approx(5.0));

	// Rotated a quarter turn about Y: the long local X axis lies along world Z.
	cell->set_size(Vector3(40, 10, 10));
	cell->set_transform(Transform3D(Basis(Vector3(0, 1, 0), Math::PI * 0.5), Vector3(100, 0, 0)));
	CHECK(cell->contains(Vector3(100, 0, 15)));
	CHECK(cell->distance_to(Vector3(110, 0, 0)) == doctest::Approx(5.0));

	// Uniform scale scales the distance.
	cell->set_transform(Transform3D(Basis().scaled(Vector3(2, 2, 2)), Vector3()));
	CHECK(cell->distance_to(Vector3(0, 0, 13)) == doctest::Approx(3.0));
	memdelete(cell);
}

TEST_CASE("[SceneTree][LevelStreaming] Saved state data") {
	LevelStreamer3D *streamer = memnew(LevelStreamer3D);
	streamer->set_active(false);
	SceneTree::get_singleton()->get_root()->add_child(streamer);
	LevelStreamCell3D *cell = _cell("shop");
	streamer->add_child(cell);

	CHECK(streamer->get_cell_state("shop") == LevelStreamer3D::CELL_UNLOADED);
	CHECK(streamer->get_cell_content("shop") == nullptr);
	CHECK(streamer->get_cell_state_data("shop").is_empty());
	Dictionary door;
	door["open"] = true;
	Dictionary data;
	data["Door"] = door;
	streamer->set_cell_state_data("shop", data);
	CHECK(streamer->get_cell_state_data("shop") == data);
	// A copy: changing the caller's dictionary does not change the saved state.
	data["Door"] = Dictionary();
	CHECK(streamer->get_cell_state_data("shop") != data);

	// Prefetching a point inside no cell is ready at once.
	CHECK(streamer->is_position_ready(Vector3(1000, 0, 0)));
	CHECK(streamer->get_cells_at(Vector3(0, 0, 0)).size() == 1);
	memdelete(streamer);
}

} // namespace TestLevelStreaming
