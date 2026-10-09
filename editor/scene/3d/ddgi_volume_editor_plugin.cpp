/**************************************************************************/
/*  ddgi_volume_editor_plugin.cpp                                         */
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

#include "ddgi_volume_editor_plugin.h"

#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "scene/main/viewport.h"
#include "scene/main/scene_tree.h"

void DDGIVolumeEditorPlugin::_bake() {
	if (!volume) {
		return;
	}
	Ref<DDGIProbeData> data = volume->get_probe_data();
	String path = data.is_valid() ? data->get_path() : String();
	if (!path.is_resource_file()) {
		// New data, or data embedded in a scene: ask where to save it. The
		// atlases are megabytes; they don't belong in a text scene file.
		String scene_path = get_tree()->get_edited_scene_root() ? get_tree()->get_edited_scene_root()->get_scene_file_path() : String();
		if (scene_path.is_empty()) {
			path = "res://" + volume->get_name() + ".ddgi.res";
		} else {
			path = scene_path.get_basename() + "." + volume->get_name() + ".ddgi.res";
		}
		probe_file->set_current_path(path);
		probe_file->popup_file_dialog();
		return;
	}
	_bake_and_save(path);
}

void DDGIVolumeEditorPlugin::_bake_and_save(const String &p_path) {
	probe_file->hide();
	if (!volume) {
		return;
	}
	Ref<DDGIProbeData> previous = volume->get_probe_data();
	Ref<DDGIProbeData> data = volume->bake();
	if (data.is_null()) {
		return; // Cancelled, or the bake printed why it failed.
	}
	if (previous.is_valid() && previous->get_path() == p_path) {
		// Rebaking: the loaded resource owns the path, so update it in place
		// (a second resource with the same path is a "cyclic inclusion" error).
		previous->set_data(data->get_data());
		data = previous;
		volume->set_probe_data(data);
	} else {
		// Replaces whatever resource is cached for the path.
		data->set_path(p_path, true);
	}
	Error err = ResourceSaver::save(data, p_path, ResourceSaver::FLAG_CHANGE_PATH | ResourceSaver::FLAG_COMPRESS);
	if (err != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Couldn't save the DDGI probe data to \"%s\"."), p_path));
		return;
	}
	if (volume->get_bake_mode() == DDGIVolume::BAKE_MODE_DYNAMIC) {
		volume->set_bake_mode(DDGIVolume::BAKE_MODE_BAKED_DYNAMIC);
	}
	EditorUndoRedoManager::get_singleton()->set_history_as_unsaved(EditorNode::get_editor_data().get_current_edited_scene_history_id());
}

void DDGIVolumeEditorPlugin::edit(Object *p_object) {
	DDGIVolume *v = Object::cast_to<DDGIVolume>(p_object);
	if (!v) {
		return;
	}
	volume = v;
}

bool DDGIVolumeEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("DDGIVolume");
}

void DDGIVolumeEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		bake_hb->show();
	} else {
		bake_hb->hide();
	}
}

EditorProgress *DDGIVolumeEditorPlugin::tmp_progress = nullptr;
int DDGIVolumeEditorPlugin::paused_update_modes[4] = { -1, -1, -1, -1 };

void DDGIVolumeEditorPlugin::bake_func_begin() {
	ERR_FAIL_COND(tmp_progress != nullptr);
	tmp_progress = memnew(EditorProgress("bake_ddgi", TTR("Bake DDGI"), 1000, true));
	// The editor's 3D views would render (and trace their own DDGI probes)
	// in every bake frame; pause them.
	for (int i = 0; i < 4; i++) {
		paused_update_modes[i] = -1;
		Node3DEditorViewport *v = Node3DEditor::get_singleton() ? Node3DEditor::get_singleton()->get_editor_viewport(i) : nullptr;
		if (v && v->get_viewport_node()) {
			paused_update_modes[i] = v->get_viewport_node()->get_update_mode();
			v->get_viewport_node()->set_update_mode(SubViewport::UPDATE_DISABLED);
		}
	}
}

bool DDGIVolumeEditorPlugin::bake_func_step(int p_progress, const String &p_description) {
	ERR_FAIL_NULL_V(tmp_progress, false);
	return tmp_progress->step(p_description, p_progress, false);
}

void DDGIVolumeEditorPlugin::bake_func_end() {
	for (int i = 0; i < 4; i++) {
		Node3DEditorViewport *v = Node3DEditor::get_singleton() ? Node3DEditor::get_singleton()->get_editor_viewport(i) : nullptr;
		if (v && v->get_viewport_node() && paused_update_modes[i] >= 0) {
			v->get_viewport_node()->set_update_mode(SubViewport::UpdateMode(paused_update_modes[i]));
		}
	}
	ERR_FAIL_NULL(tmp_progress);
	memdelete(tmp_progress);
	tmp_progress = nullptr;
}

DDGIVolumeEditorPlugin::DDGIVolumeEditorPlugin() {
	bake_hb = memnew(HBoxContainer);
	bake_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bake_hb->hide();
	bake = memnew(Button);
	bake->set_theme_type_variation(SceneStringName(FlatButton));
	bake->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Bake"), EditorStringName(EditorIcons)));
	bake->set_text(TTR("Bake DDGI"));
	bake->set_tooltip_text(TTR("Trace the DDGI probes until they converge and save them, so the game starts with finished indirect light (Bake Mode: Baked + Dynamic) or doesn't trace at all (Bake Mode: Baked)."));
	bake->connect(SceneStringName(pressed), callable_mp(this, &DDGIVolumeEditorPlugin::_bake));
	bake_hb->add_child(bake);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, bake_hb);
	probe_file = memnew(EditorFileDialog);
	probe_file->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	probe_file->add_filter("*.res");
	probe_file->connect("file_selected", callable_mp(this, &DDGIVolumeEditorPlugin::_bake_and_save));
	EditorInterface::get_singleton()->get_base_control()->add_child(probe_file);
	probe_file->set_title(TTR("Select path for DDGI Probe Data File"));

	DDGIVolume::bake_begin_function = bake_func_begin;
	DDGIVolume::bake_step_function = bake_func_step;
	DDGIVolume::bake_end_function = bake_func_end;
}
