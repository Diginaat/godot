/**************************************************************************/
/*  level_streamer_3d.cpp                                                 */
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

#include "level_streamer_3d.h"

#include "level_stream_cell_3d.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

static uint64_t _now_usec() {
	return OS::get_singleton()->get_ticks_usec();
}

// A cell whose scene is (or contains) one of its own ancestors' scenes would
// stream itself forever.
static bool _is_cycle(const Node *p_cell, const String &p_path) {
	for (const Node *node = p_cell; node; node = node->get_parent()) {
		if (node->get_scene_file_path() == p_path) {
			return true;
		}
	}
	return false;
}

static void _collect_persistent(Node *p_node, LocalVector<Node *> &r_nodes) {
	if (p_node->is_in_group(LevelStreamer3D::PERSISTENT_GROUP)) {
		r_nodes.push_back(p_node);
	}
	const int count = p_node->get_child_count(true);
	for (int i = 0; i < count; i++) {
		_collect_persistent(p_node->get_child(i, true), r_nodes);
	}
}

void LevelStreamer3D::_instantiate_task(void *p_cell) {
	Cell *cell = static_cast<Cell *>(p_cell);
	cell->instantiated = cell->scene->instantiate();
}

bool LevelStreamer3D::_target_position(Vector3 &r_position) const {
	const Node3D *target = nullptr;
	if (!target_path.is_empty()) {
		target = Object::cast_to<Node3D>(get_node_or_null(target_path));
	} else if (get_viewport()) {
		target = get_viewport()->get_camera_3d();
	}
	if (!target || !target->is_inside_tree()) {
		return false;
	}
	r_position = target->get_global_position();
	return true;
}

void LevelStreamer3D::_update_wanted() {
	Vector3 target;
	const bool valid = _target_position(target);
	LocalVector<StringName> neighbors;
	for (KeyValue<StringName, Cell> &entry : cells) {
		Cell &cell = entry.value;
		const LevelStreamCell3D *node = cell.node;
		if (node->is_always_loaded()) {
			cell.wanted = true;
			cell.distance = 0.0;
			continue;
		}
		if (!valid) {
			continue; // No target: keep what is loaded, start nothing new.
		}
		cell.distance = node->distance_to(target);
		const bool loaded = cell.state != CELL_UNLOADED && cell.state != CELL_FAILED;
		// Hysteresis: a loaded cell stays until the larger unload distance.
		cell.wanted = cell.distance <= (loaded ? MAX(node->get_unload_distance(), node->get_preload_distance()) : node->get_preload_distance());
		if (cell.distance <= 0.0) {
			for (const String &neighbor : node->get_neighbors()) {
				neighbors.push_back(neighbor);
			}
		}
	}
	// The cells around the one the target is in load ahead of their distance,
	// so lights, shadows and collision across the boundary are there first.
	for (const StringName &id : neighbors) {
		Cell *cell = cells.getptr(id);
		if (cell) {
			cell->wanted = true;
		}
	}
	for (const StringName &id : pinned) {
		Cell *cell = cells.getptr(id);
		if (cell) {
			cell->wanted = true;
			cell->distance = 0.0;
		}
	}
}

void LevelStreamer3D::_poll_loads() {
	for (KeyValue<StringName, Cell> &entry : cells) {
		Cell &cell = entry.value;
		if (cell.state != CELL_LOADING) {
			continue;
		}
		const ResourceLoader::ThreadLoadStatus status = ResourceLoader::load_threaded_get_status(cell.path);
		if (status == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			continue;
		}
		Error error = OK;
		Ref<Resource> resource;
		if (status != ResourceLoader::THREAD_LOAD_INVALID_RESOURCE) {
			// Collects the result and releases the request.
			resource = ResourceLoader::load_threaded_get(cell.path, &error);
		}
		cell.scene = resource;
		if (cell.scene.is_null()) {
			cell.state = CELL_FAILED;
			cell.retry_usec = _now_usec() + uint64_t(retry_delay * 1000000.0);
			stats.failures++;
			ERR_PRINT(vformat("LevelStreamer3D: cell \"%s\" could not load \"%s\".", entry.key, cell.path));
			emit_signal(SNAME("cell_failed"), entry.key, cell.path);
			continue;
		}
		if (!cell.wanted || cell.node->get_scene_path() != cell.path) {
			// Not needed any more (the target turned back), or the cell now
			// names another scene: drop it; a new request follows if wanted.
			cell.scene.unref();
			cell.state = CELL_UNLOADED;
			stats.stale++;
			continue;
		}
		cell.state = CELL_INSTANTIATING;
		if (threaded_instantiation) {
			cell.instantiated = nullptr;
			cell.task = WorkerThreadPool::get_singleton()->add_native_task(&LevelStreamer3D::_instantiate_task, &cell, false, SNAME("LevelStreamer3D instantiate"));
		}
	}
	for (uint32_t i = 0; i < orphan_loads.size();) {
		if (ResourceLoader::load_threaded_get_status(orphan_loads[i]) == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			i++;
			continue;
		}
		ResourceLoader::load_threaded_get(orphan_loads[i]);
		orphan_loads.remove_at_unordered(i);
	}
}

void LevelStreamer3D::_start_loads() {
	int in_flight = 0;
	sorted.clear();
	const uint64_t now = _now_usec();
	for (KeyValue<StringName, Cell> &entry : cells) {
		Cell &cell = entry.value;
		if (cell.state == CELL_LOADING) {
			in_flight++;
		} else if (cell.wanted && (cell.state == CELL_UNLOADED || (cell.state == CELL_FAILED && now >= cell.retry_usec))) {
			sorted.push_back(&cell);
		}
	}
	if (in_flight >= max_loading || sorted.is_empty()) {
		return;
	}
	struct ByDistance {
		bool operator()(const Cell *p_a, const Cell *p_b) const { return p_a->distance < p_b->distance; }
	};
	sorted.sort_custom<ByDistance>();
	for (Cell *cell : sorted) {
		if (in_flight >= max_loading) {
			break;
		}
		const String path = cell->node->get_scene_path();
		if (path.is_empty() || _is_cycle(cell->node, path)) {
			continue;
		}
		cell->path = path;
		cell->requested_usec = now;
		stats.requests++;
		if (ResourceLoader::load_threaded_request(path, "PackedScene") != OK) {
			cell->state = CELL_FAILED;
			cell->retry_usec = now + uint64_t(retry_delay * 1000000.0);
			stats.failures++;
			emit_signal(SNAME("cell_failed"), cell->node->get_cell_id(), path);
			continue;
		}
		cell->state = CELL_LOADING;
		in_flight++;
	}
}

bool LevelStreamer3D::_reap_task(Cell &p_cell) {
	if (p_cell.task == WorkerThreadPool::INVALID_TASK_ID) {
		return true;
	}
	if (!WorkerThreadPool::get_singleton()->is_task_completed(p_cell.task)) {
		return false;
	}
	WorkerThreadPool::get_singleton()->wait_for_task_completion(p_cell.task);
	p_cell.task = WorkerThreadPool::INVALID_TASK_ID;
	return true;
}

void LevelStreamer3D::_activate_and_unload() {
	int unloads = max_activations_per_frame;
	sorted.clear();
	for (KeyValue<StringName, Cell> &entry : cells) {
		Cell &cell = entry.value;
		if (cell.state == CELL_INSTANTIATING) {
			if (cell.wanted) {
				sorted.push_back(&cell);
			} else if (_reap_task(cell)) {
				_discard(cell);
				stats.stale++;
			}
		} else if ((cell.state == CELL_ACTIVE || cell.state == CELL_READY) && !cell.wanted && unloads > 0) {
			_unload(cell);
			unloads--;
		}
	}
	struct ByDistance {
		bool operator()(const Cell *p_a, const Cell *p_b) const { return p_a->distance < p_b->distance; }
	};
	sorted.sort_custom<ByDistance>();
	int budget = max_activations_per_frame;
	for (Cell *cell : sorted) {
		if (budget <= 0) {
			break;
		}
		if (!_reap_task(*cell)) {
			continue;
		}
		const uint64_t begin = _now_usec();
		Node *content = threaded_instantiation ? cell->instantiated : cell->scene->instantiate();
		cell->instantiated = nullptr;
		cell->scene.unref(); // The instance holds what it uses.
		if (!content) {
			cell->state = CELL_FAILED;
			cell->retry_usec = begin + uint64_t(retry_delay * 1000000.0);
			stats.failures++;
			emit_signal(SNAME("cell_failed"), cell->node->get_cell_id(), cell->path);
			continue;
		}
		_activate(*cell, content);
		const uint64_t elapsed = _now_usec() - begin;
		stats.last_activation_usec = elapsed;
		stats.max_activation_usec = MAX(stats.max_activation_usec, elapsed);
		budget--;
	}
}

void LevelStreamer3D::_activate(Cell &p_cell, Node *p_content) {
	const StringName id = p_cell.node->get_cell_id();
	// Before the content enters the tree, so _ready() sees the saved state.
	_restore_state(id, p_content);
	emit_signal(SNAME("cell_activating"), id, p_content);
	p_cell.content = p_content;
	p_cell.node->add_child(p_content);
	p_cell.state = CELL_ACTIVE;
	// Bodies join the physics space and navigation regions sync on the
	// physics step: the cell is safe to enter after a few of them.
	p_cell.ready_physics_frame = Engine::get_singleton()->get_physics_frames() + ready_physics_frames;
	stats.activations++;
	emit_signal(SNAME("cell_activated"), id, p_content);
}

void LevelStreamer3D::_unload(Cell &p_cell) {
	const StringName id = p_cell.node->get_cell_id();
	Node *content = p_cell.content;
	emit_signal(SNAME("cell_unloading"), id, content);
	_save_state(id, content);
	p_cell.content = nullptr;
	p_cell.state = CELL_UNLOADED;
	if (content->get_parent()) {
		content->get_parent()->remove_child(content);
	}
	content->queue_free();
	stats.unloads++;
	emit_signal(SNAME("cell_unloaded"), id);
}

void LevelStreamer3D::_discard(Cell &p_cell) {
	if (p_cell.task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(p_cell.task);
		p_cell.task = WorkerThreadPool::INVALID_TASK_ID;
	}
	if (p_cell.instantiated) {
		memdelete(p_cell.instantiated);
		p_cell.instantiated = nullptr;
	}
	if (p_cell.state == CELL_LOADING) {
		// A threaded request can't be cancelled; collect it later.
		orphan_loads.push_back(p_cell.path);
	}
	p_cell.scene.unref();
	if (p_cell.content) {
		_save_state(p_cell.node->get_cell_id(), p_cell.content);
		p_cell.content->queue_free();
		p_cell.content = nullptr;
	}
	p_cell.state = CELL_UNLOADED;
}

void LevelStreamer3D::_save_state(const StringName &p_id, Node *p_content) {
	LocalVector<Node *> nodes;
	_collect_persistent(p_content, nodes);
	if (nodes.is_empty()) {
		return;
	}
	Dictionary state = saved_state.has(p_id) ? saved_state[p_id] : Dictionary();
	for (Node *node : nodes) {
		if (node->has_method(SNAME("_save_stream_state"))) {
			state[String(p_content->get_path_to(node))] = node->call(SNAME("_save_stream_state"));
		}
	}
	saved_state[p_id] = state;
}

void LevelStreamer3D::_restore_state(const StringName &p_id, Node *p_content) {
	const Dictionary *state = saved_state.getptr(p_id);
	if (!state) {
		return;
	}
	for (const KeyValue<Variant, Variant> &entry : *state) {
		Node *node = p_content->get_node_or_null(NodePath(String(entry.key)));
		if (node && node->has_method(SNAME("_load_stream_state"))) {
			node->call(SNAME("_load_stream_state"), entry.value);
		}
	}
}

void LevelStreamer3D::_update_destination() {
	for (KeyValue<StringName, Cell> &entry : cells) {
		Cell &cell = entry.value;
		if (cell.state == CELL_ACTIVE && Engine::get_singleton()->get_physics_frames() >= cell.ready_physics_frame) {
			cell.state = CELL_READY;
			const uint64_t delay = _now_usec() - cell.requested_usec;
			stats.last_ready_delay_usec = delay;
			stats.max_ready_delay_usec = MAX(stats.max_ready_delay_usec, delay);
			emit_signal(SNAME("cell_ready"), entry.key);
		}
	}
	if (!destination_pending) {
		return;
	}
	for (const StringName &id : pinned) {
		const Cell *cell = cells.getptr(id);
		if (cell && cell->state != CELL_READY) {
			return;
		}
	}
	destination_pending = false;
	emit_signal(SNAME("destination_ready"), destination);
}

void LevelStreamer3D::_update() {
	if (cells.is_empty()) {
		return;
	}
	_update_wanted();
	_poll_loads();
	_start_loads();
	_activate_and_unload();
	_update_destination();
}

bool LevelStreamer3D::_is_editor_preview() const {
#ifdef TOOLS_ENABLED
	return preview_in_editor && Engine::get_singleton()->is_editor_hint();
#else
	return false;
#endif
}

void LevelStreamer3D::_preview(Cell &p_cell) {
	// Editor only: shows every cell's scene so a level can be laid out. The
	// preview has no owner and is internal, so it is never saved.
	if (p_cell.content) {
		p_cell.content->queue_free();
		p_cell.content = nullptr;
	}
	if (!_is_editor_preview()) {
		return;
	}
	const String path = p_cell.node->get_scene_path();
	if (path.is_empty() || _is_cycle(p_cell.node, path) || !ResourceLoader::exists(path, "PackedScene")) {
		return;
	}
	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene");
	if (scene.is_null()) {
		return;
	}
	p_cell.content = scene->instantiate();
	if (p_cell.content) {
		p_cell.node->add_child(p_cell.content, false, INTERNAL_MODE_BACK);
	}
}

void LevelStreamer3D::_update_processing() {
	set_process_internal(active && is_inside_tree() && !cells.is_empty() && !Engine::get_singleton()->is_editor_hint());
}

void LevelStreamer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update_processing();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update();
		} break;
	}
}

void LevelStreamer3D::register_cell(LevelStreamCell3D *p_cell) {
	const StringName id = p_cell->get_cell_id();
	if (id == StringName() || cells.has(id)) {
		rejected.push_back(p_cell);
		if (id != StringName() && !Engine::get_singleton()->is_editor_hint()) {
			ERR_PRINT(vformat("LevelStreamer3D: two cells use the ID \"%s\"; \"%s\" is not streamed.", id, p_cell->get_path()));
		}
		p_cell->update_configuration_warnings();
		return;
	}
	Cell &cell = cells.insert(id, Cell())->value;
	cell.node = p_cell;
	if (_is_editor_preview()) {
		_preview(cell);
	}
	_update_processing();
	update_configuration_warnings();
}

void LevelStreamer3D::unregister_cell(LevelStreamCell3D *p_cell) {
	if (rejected.erase(p_cell)) {
		return;
	}
	for (KeyValue<StringName, Cell> &entry : cells) {
		if (entry.value.node != p_cell) {
			continue;
		}
		const StringName id = entry.key;
		_discard(entry.value);
		cells.erase(id);
		pinned.erase(id);
		// A cell rejected for this ID can take its place.
		for (uint32_t i = 0; i < rejected.size(); i++) {
			if (rejected[i]->get_cell_id() == id) {
				LevelStreamCell3D *next = rejected[i];
				rejected.remove_at(i);
				register_cell(next);
				next->update_configuration_warnings();
				break;
			}
		}
		break;
	}
	_update_processing();
	update_configuration_warnings();
}

void LevelStreamer3D::cell_id_changed(LevelStreamCell3D *p_cell, const StringName &p_old_id) {
	unregister_cell(p_cell);
	register_cell(p_cell);
}

void LevelStreamer3D::cell_scene_changed(LevelStreamCell3D *p_cell) {
	Cell *cell = cells.getptr(p_cell->get_cell_id());
	if (cell && cell->node == p_cell && _is_editor_preview()) {
		_preview(*cell);
	}
	// At run time the next load uses the new scene; a load in flight for the
	// old one is dropped when it arrives.
}

bool LevelStreamer3D::is_duplicate_cell(const LevelStreamCell3D *p_cell) const {
	for (const LevelStreamCell3D *cell : rejected) {
		if (cell == p_cell) {
			return true;
		}
	}
	return false;
}

bool LevelStreamer3D::has_cell(const StringName &p_id) const {
	return cells.has(p_id);
}

void LevelStreamer3D::set_target_path(const NodePath &p_path) {
	target_path = p_path;
	update_configuration_warnings();
}

NodePath LevelStreamer3D::get_target_path() const {
	return target_path;
}

void LevelStreamer3D::set_active(bool p_active) {
	active = p_active;
	_update_processing();
}

bool LevelStreamer3D::is_active() const {
	return active;
}

void LevelStreamer3D::set_max_loading(int p_count) {
	max_loading = MAX(p_count, 1);
}

int LevelStreamer3D::get_max_loading() const {
	return max_loading;
}

void LevelStreamer3D::set_max_activations_per_frame(int p_count) {
	max_activations_per_frame = MAX(p_count, 1);
}

int LevelStreamer3D::get_max_activations_per_frame() const {
	return max_activations_per_frame;
}

void LevelStreamer3D::set_threaded_instantiation(bool p_enabled) {
	threaded_instantiation = p_enabled;
}

bool LevelStreamer3D::is_threaded_instantiation() const {
	return threaded_instantiation;
}

void LevelStreamer3D::set_ready_physics_frames(int p_frames) {
	ready_physics_frames = MAX(p_frames, 0);
}

int LevelStreamer3D::get_ready_physics_frames() const {
	return ready_physics_frames;
}

void LevelStreamer3D::set_retry_delay(real_t p_seconds) {
	retry_delay = MAX(p_seconds, (real_t)0.0);
}

real_t LevelStreamer3D::get_retry_delay() const {
	return retry_delay;
}

void LevelStreamer3D::set_preview_in_editor(bool p_enabled) {
	if (preview_in_editor == p_enabled) {
		return;
	}
	preview_in_editor = p_enabled;
	if (Engine::get_singleton()->is_editor_hint()) {
		for (KeyValue<StringName, Cell> &entry : cells) {
			_preview(entry.value);
		}
	}
}

bool LevelStreamer3D::is_preview_in_editor() const {
	return preview_in_editor;
}

LevelStreamer3D::CellState LevelStreamer3D::get_cell_state(const StringName &p_id) const {
	const Cell *cell = cells.getptr(p_id);
	return cell ? cell->state : CELL_UNLOADED;
}

Node *LevelStreamer3D::get_cell_content(const StringName &p_id) const {
	const Cell *cell = cells.getptr(p_id);
	return cell ? cell->content : nullptr;
}

LevelStreamCell3D *LevelStreamer3D::get_cell(const StringName &p_id) const {
	const Cell *cell = cells.getptr(p_id);
	return cell ? cell->node : nullptr;
}

bool LevelStreamer3D::is_cell_ready(const StringName &p_id) const {
	return get_cell_state(p_id) == CELL_READY;
}

PackedStringArray LevelStreamer3D::get_cell_ids() const {
	PackedStringArray ids;
	for (const KeyValue<StringName, Cell> &entry : cells) {
		ids.push_back(entry.key);
	}
	return ids;
}

PackedStringArray LevelStreamer3D::get_cells_at(const Vector3 &p_global_point) const {
	PackedStringArray ids;
	for (const KeyValue<StringName, Cell> &entry : cells) {
		if (entry.value.node->contains(p_global_point)) {
			ids.push_back(entry.key);
		}
	}
	return ids;
}

bool LevelStreamer3D::is_position_ready(const Vector3 &p_global_point) const {
	for (const KeyValue<StringName, Cell> &entry : cells) {
		if (entry.value.state != CELL_READY && entry.value.node->contains(p_global_point)) {
			return false;
		}
	}
	return true;
}

void LevelStreamer3D::prefetch_position(const Vector3 &p_global_point) {
	pinned.clear();
	for (const String &id : get_cells_at(p_global_point)) {
		pinned.push_back(id);
		for (const String &neighbor : cells[id].node->get_neighbors()) {
			if (cells.has(neighbor) && !pinned.has(neighbor)) {
				pinned.push_back(neighbor);
			}
		}
	}
	destination = p_global_point;
	destination_pending = true;
}

void LevelStreamer3D::clear_prefetch() {
	pinned.clear();
	destination_pending = false;
}

Dictionary LevelStreamer3D::get_cell_state_data(const StringName &p_id) const {
	const Cell *cell = cells.getptr(p_id);
	Dictionary state = saved_state.has(p_id) ? saved_state[p_id].duplicate() : Dictionary();
	if (cell && cell->content) {
		// Active cells report their current state, not the last saved one.
		LocalVector<Node *> nodes;
		_collect_persistent(cell->content, nodes);
		for (Node *node : nodes) {
			if (node->has_method(SNAME("_save_stream_state"))) {
				state[String(cell->content->get_path_to(node))] = node->call(SNAME("_save_stream_state"));
			}
		}
	}
	return state;
}

void LevelStreamer3D::set_cell_state_data(const StringName &p_id, const Dictionary &p_data) {
	saved_state[p_id] = p_data.duplicate();
	const Cell *cell = cells.getptr(p_id);
	if (cell && cell->content) {
		_restore_state(p_id, cell->content);
	}
}

Dictionary LevelStreamer3D::get_statistics() const {
	Dictionary result;
	int counts[CELL_FAILED + 1] = {};
	for (const KeyValue<StringName, Cell> &entry : cells) {
		counts[entry.value.state]++;
	}
	result["cells"] = cells.size();
	result["unloaded"] = counts[CELL_UNLOADED];
	result["loading"] = counts[CELL_LOADING];
	result["instantiating"] = counts[CELL_INSTANTIATING];
	result["active"] = counts[CELL_ACTIVE];
	result["ready"] = counts[CELL_READY];
	result["failed"] = counts[CELL_FAILED];
	result["requests"] = stats.requests;
	result["activations"] = stats.activations;
	result["unloads"] = stats.unloads;
	result["failures"] = stats.failures;
	result["stale"] = stats.stale;
	result["last_activation_usec"] = stats.last_activation_usec;
	result["max_activation_usec"] = stats.max_activation_usec;
	result["last_ready_delay_usec"] = stats.last_ready_delay_usec;
	result["max_ready_delay_usec"] = stats.max_ready_delay_usec;
	return result;
}

void LevelStreamer3D::update_now() {
	_update();
}

PackedStringArray LevelStreamer3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!target_path.is_empty() && is_inside_tree()) {
		const Node *target = get_node_or_null(target_path);
		if (!target) {
			warnings.push_back(RTR("The target node does not exist. Cells stream around the target, or the current camera when no target is set."));
		} else if (!Object::cast_to<Node3D>(target)) {
			warnings.push_back(RTR("The target must be a Node3D."));
		}
	}
	if (cells.is_empty() && rejected.is_empty()) {
		warnings.push_back(RTR("Add LevelStreamCell3D nodes below this node: each streams one scene."));
	}
	if (!rejected.is_empty()) {
		warnings.push_back(vformat(RTR("%d cells have no ID or an ID another cell uses; they are not streamed."), rejected.size()));
	}
	return warnings;
}

LevelStreamer3D::~LevelStreamer3D() {
	for (KeyValue<StringName, Cell> &entry : cells) {
		_discard(entry.value);
	}
	for (const String &path : orphan_loads) {
		ResourceLoader::load_threaded_get(path);
	}
}

void LevelStreamer3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_path", "path"), &LevelStreamer3D::set_target_path);
	ClassDB::bind_method(D_METHOD("get_target_path"), &LevelStreamer3D::get_target_path);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &LevelStreamer3D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &LevelStreamer3D::is_active);
	ClassDB::bind_method(D_METHOD("set_max_loading", "count"), &LevelStreamer3D::set_max_loading);
	ClassDB::bind_method(D_METHOD("get_max_loading"), &LevelStreamer3D::get_max_loading);
	ClassDB::bind_method(D_METHOD("set_max_activations_per_frame", "count"), &LevelStreamer3D::set_max_activations_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_activations_per_frame"), &LevelStreamer3D::get_max_activations_per_frame);
	ClassDB::bind_method(D_METHOD("set_threaded_instantiation", "enabled"), &LevelStreamer3D::set_threaded_instantiation);
	ClassDB::bind_method(D_METHOD("is_threaded_instantiation"), &LevelStreamer3D::is_threaded_instantiation);
	ClassDB::bind_method(D_METHOD("set_ready_physics_frames", "frames"), &LevelStreamer3D::set_ready_physics_frames);
	ClassDB::bind_method(D_METHOD("get_ready_physics_frames"), &LevelStreamer3D::get_ready_physics_frames);
	ClassDB::bind_method(D_METHOD("set_retry_delay", "seconds"), &LevelStreamer3D::set_retry_delay);
	ClassDB::bind_method(D_METHOD("get_retry_delay"), &LevelStreamer3D::get_retry_delay);
	ClassDB::bind_method(D_METHOD("set_preview_in_editor", "enabled"), &LevelStreamer3D::set_preview_in_editor);
	ClassDB::bind_method(D_METHOD("is_preview_in_editor"), &LevelStreamer3D::is_preview_in_editor);

	ClassDB::bind_method(D_METHOD("get_cell_state", "cell_id"), &LevelStreamer3D::get_cell_state);
	ClassDB::bind_method(D_METHOD("get_cell_content", "cell_id"), &LevelStreamer3D::get_cell_content);
	ClassDB::bind_method(D_METHOD("get_cell", "cell_id"), &LevelStreamer3D::get_cell);
	ClassDB::bind_method(D_METHOD("is_cell_ready", "cell_id"), &LevelStreamer3D::is_cell_ready);
	ClassDB::bind_method(D_METHOD("get_cell_ids"), &LevelStreamer3D::get_cell_ids);
	ClassDB::bind_method(D_METHOD("get_cells_at", "global_point"), &LevelStreamer3D::get_cells_at);
	ClassDB::bind_method(D_METHOD("is_position_ready", "global_point"), &LevelStreamer3D::is_position_ready);
	ClassDB::bind_method(D_METHOD("prefetch_position", "global_point"), &LevelStreamer3D::prefetch_position);
	ClassDB::bind_method(D_METHOD("clear_prefetch"), &LevelStreamer3D::clear_prefetch);
	ClassDB::bind_method(D_METHOD("get_cell_state_data", "cell_id"), &LevelStreamer3D::get_cell_state_data);
	ClassDB::bind_method(D_METHOD("set_cell_state_data", "cell_id", "data"), &LevelStreamer3D::set_cell_state_data);
	ClassDB::bind_method(D_METHOD("get_statistics"), &LevelStreamer3D::get_statistics);
	ClassDB::bind_method(D_METHOD("update_now"), &LevelStreamer3D::update_now);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_target_path", "get_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_loading", PROPERTY_HINT_RANGE, "1,16,1,or_greater"), "set_max_loading", "get_max_loading");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_activations_per_frame", PROPERTY_HINT_RANGE, "1,16,1,or_greater"), "set_max_activations_per_frame", "get_max_activations_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_instantiation"), "set_threaded_instantiation", "is_threaded_instantiation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ready_physics_frames", PROPERTY_HINT_RANGE, "0,16,1,or_greater"), "set_ready_physics_frames", "get_ready_physics_frames");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "retry_delay", PROPERTY_HINT_RANGE, "0,60,0.1,or_greater,suffix:s"), "set_retry_delay", "get_retry_delay");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preview_in_editor"), "set_preview_in_editor", "is_preview_in_editor");

	ADD_SIGNAL(MethodInfo("cell_activating", PropertyInfo(Variant::STRING_NAME, "cell_id"), PropertyInfo(Variant::OBJECT, "content", PROPERTY_HINT_RESOURCE_TYPE, "Node")));
	ADD_SIGNAL(MethodInfo("cell_activated", PropertyInfo(Variant::STRING_NAME, "cell_id"), PropertyInfo(Variant::OBJECT, "content", PROPERTY_HINT_RESOURCE_TYPE, "Node")));
	ADD_SIGNAL(MethodInfo("cell_ready", PropertyInfo(Variant::STRING_NAME, "cell_id")));
	ADD_SIGNAL(MethodInfo("cell_unloading", PropertyInfo(Variant::STRING_NAME, "cell_id"), PropertyInfo(Variant::OBJECT, "content", PROPERTY_HINT_RESOURCE_TYPE, "Node")));
	ADD_SIGNAL(MethodInfo("cell_unloaded", PropertyInfo(Variant::STRING_NAME, "cell_id")));
	ADD_SIGNAL(MethodInfo("cell_failed", PropertyInfo(Variant::STRING_NAME, "cell_id"), PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("destination_ready", PropertyInfo(Variant::VECTOR3, "global_point")));

	BIND_ENUM_CONSTANT(CELL_UNLOADED);
	BIND_ENUM_CONSTANT(CELL_LOADING);
	BIND_ENUM_CONSTANT(CELL_INSTANTIATING);
	BIND_ENUM_CONSTANT(CELL_ACTIVE);
	BIND_ENUM_CONSTANT(CELL_READY);
	BIND_ENUM_CONSTANT(CELL_FAILED);
}
