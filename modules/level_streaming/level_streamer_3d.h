/**************************************************************************/
/*  level_streamer_3d.h                                                   */
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

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/packed_scene.h"

class LevelStreamCell3D;

// Streams the LevelStreamCell3D nodes below it around a target node.
// Scenes load with threaded ResourceLoader requests and instantiate on a
// worker thread; tree changes happen on the main thread, a bounded number
// per frame. Each cell has a preload and a larger unload distance, so a
// target at a boundary does not load and unload it over and over.
//
// Per-frame cost: one distance test per cell while the node is active; a
// project without a streamer pays nothing.
class LevelStreamer3D : public Node3D {
	GDCLASS(LevelStreamer3D, Node3D);

public:
	enum CellState {
		CELL_UNLOADED,
		CELL_LOADING, // Threaded ResourceLoader request in flight.
		CELL_INSTANTIATING, // Scene loaded; instantiating (worker thread) or queued.
		CELL_ACTIVE, // Content in the tree; collision and navigation registering.
		CELL_READY, // Active for ready_physics_frames physics frames: safe to enter.
		CELL_FAILED, // The scene did not load; retried after retry_delay.
	};

	static constexpr const char *PERSISTENT_GROUP = "level_stream_persistent";

private:
	struct Cell {
		LevelStreamCell3D *node = nullptr;
		CellState state = CELL_UNLOADED;
		String path; // The scene requested, kept to spot a changed scene_path.
		Ref<PackedScene> scene;
		Node *content = nullptr;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		Node *instantiated = nullptr; // Written by the worker task.
		uint64_t ready_physics_frame = 0;
		uint64_t retry_usec = 0;
		uint64_t requested_usec = 0;
		real_t distance = 0.0;
		bool wanted = false;
	};

	NodePath target_path;
	bool active = true;
	int max_loading = 2;
	int max_activations_per_frame = 1;
	bool threaded_instantiation = true;
	int ready_physics_frames = 2;
	real_t retry_delay = 2.0;
	bool preview_in_editor = true;

	HashMap<StringName, Cell> cells; // Insertion ordered.
	LocalVector<LevelStreamCell3D *> rejected; // No ID, or a duplicate ID.
	HashMap<StringName, Dictionary> saved_state; // Cell ID -> node path -> state.
	LocalVector<StringName> pinned; // Cells kept loaded for prefetch_position().
	bool destination_pending = false;
	Vector3 destination;
	LocalVector<Cell *> sorted; // Scratch, wanted cells by distance.
	LocalVector<String> orphan_loads; // Requests of removed cells, collected when done.

	struct Statistics {
		uint64_t requests = 0;
		uint64_t activations = 0;
		uint64_t unloads = 0;
		uint64_t failures = 0;
		uint64_t stale = 0;
		uint64_t last_activation_usec = 0;
		uint64_t max_activation_usec = 0;
		uint64_t last_ready_delay_usec = 0;
		uint64_t max_ready_delay_usec = 0;
	} stats;

	static void _instantiate_task(void *p_cell);
	bool _target_position(Vector3 &r_position) const;
	void _update();
	void _update_wanted();
	void _poll_loads();
	void _start_loads();
	void _activate_and_unload();
	bool _reap_task(Cell &p_cell);
	void _activate(Cell &p_cell, Node *p_content);
	void _unload(Cell &p_cell);
	void _discard(Cell &p_cell);
	void _save_state(const StringName &p_id, Node *p_content);
	void _restore_state(const StringName &p_id, Node *p_content);
	void _update_destination();
	void _update_processing();
	bool _is_editor_preview() const;
	void _preview(Cell &p_cell);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Called by LevelStreamCell3D.
	void register_cell(LevelStreamCell3D *p_cell);
	void unregister_cell(LevelStreamCell3D *p_cell);
	void cell_id_changed(LevelStreamCell3D *p_cell, const StringName &p_old_id);
	void cell_scene_changed(LevelStreamCell3D *p_cell);
	bool is_duplicate_cell(const LevelStreamCell3D *p_cell) const;
	bool has_cell(const StringName &p_id) const;

	void set_target_path(const NodePath &p_path);
	NodePath get_target_path() const;

	void set_active(bool p_active);
	bool is_active() const;

	void set_max_loading(int p_count);
	int get_max_loading() const;

	void set_max_activations_per_frame(int p_count);
	int get_max_activations_per_frame() const;

	void set_threaded_instantiation(bool p_enabled);
	bool is_threaded_instantiation() const;

	void set_ready_physics_frames(int p_frames);
	int get_ready_physics_frames() const;

	void set_retry_delay(real_t p_seconds);
	real_t get_retry_delay() const;

	void set_preview_in_editor(bool p_enabled);
	bool is_preview_in_editor() const;

	CellState get_cell_state(const StringName &p_id) const;
	Node *get_cell_content(const StringName &p_id) const;
	LevelStreamCell3D *get_cell(const StringName &p_id) const;
	bool is_cell_ready(const StringName &p_id) const;
	PackedStringArray get_cell_ids() const;
	PackedStringArray get_cells_at(const Vector3 &p_global_point) const;
	bool is_position_ready(const Vector3 &p_global_point) const;

	void prefetch_position(const Vector3 &p_global_point);
	void clear_prefetch();

	Dictionary get_cell_state_data(const StringName &p_id) const;
	void set_cell_state_data(const StringName &p_id, const Dictionary &p_data);

	Dictionary get_statistics() const;
	void update_now();

	PackedStringArray get_configuration_warnings() const override;

	~LevelStreamer3D();
};

VARIANT_ENUM_CAST(LevelStreamer3D::CellState);
