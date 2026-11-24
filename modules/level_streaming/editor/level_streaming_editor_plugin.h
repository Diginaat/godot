/**************************************************************************/
/*  level_streaming_editor_plugin.h                                       */
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

#include "editor/export/editor_export_plugin.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_gizmos.h"

// Draws a LevelStreamCell3D in the 3D editor: its bounds, and the preload and
// unload distances around them.
class LevelStreamCell3DGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(LevelStreamCell3DGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	bool has_gizmo(Node3D *p_spatial) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;

	LevelStreamCell3DGizmoPlugin();
};

// Cell scenes are referenced by path (they load only when streamed), so an
// export of selected scenes would miss them: this names them as runtime
// dependencies of the scenes that hold the cells.
class LevelStreamingExportPlugin : public EditorExportPlugin {
	GDCLASS(LevelStreamingExportPlugin, EditorExportPlugin);

public:
	virtual String get_name() const override { return "LevelStreaming"; }
	virtual Vector<String> get_runtime_dependencies(const String &p_path) const override;
};

class LevelStreamingEditorPlugin : public EditorPlugin {
	GDCLASS(LevelStreamingEditorPlugin, EditorPlugin);

	Ref<LevelStreamCell3DGizmoPlugin> gizmo_plugin;

public:
	virtual String get_plugin_name() const override { return "LevelStreaming"; }

	LevelStreamingEditorPlugin();
};
