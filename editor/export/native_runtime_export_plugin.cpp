/**************************************************************************/
/*  native_runtime_export_plugin.cpp                                      */
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

#include "native_runtime_export_plugin.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/export/editor_export_platform.h"

// PhysX GPU dynamics, NVIDIA Blast (destruction) and Flow (gas, fire). The
// game needs them where the editor has them: next to the executable.
static const char *PHYSX_RUNTIME_FILES[] = {
	"PhysXGpu_64.dll",
	"NvBlast.dll",
	"NvBlastGlobals.dll",
	"NvBlastExtAuthoring.dll",
	"NvBlastExtShaders.dll",
	"nvflow.dll",
	"nvflowext.dll",
};

// The signed release files the DLSS installer puts next to the editor
// (editor/settings/streamline_installer.cpp), with their license texts. They
// are only shipped inside games made with this editor, never with the
// editor or the export templates (NVIDIA's terms).
static const char *STREAMLINE_RUNTIME_FILES[] = {
	"sl.interposer.dll",
	"sl.common.dll",
	"sl.dlss.dll",
	"sl.dlss_d.dll",
	"sl.dlss_g.dll",
	"sl.reflex.dll",
	"sl.pcl.dll",
	"sl.nis.dll",
	"sl.deepdvc.dll",
	"sl.directsr.dll",
	"sl.nvperf.dll",
	"nvngx_dlss.dll",
	"nvngx_dlssd.dll",
	"nvngx_dlssg.dll",
	"nvngx_deepdvc.dll",
	"NvLowLatencyVk.dll",
	"nvngx_dlss.license.txt",
	"reflex.license.txt",
	"nis.license.txt",
};

bool NativeRuntimeExportPlugin::supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const {
	return p_export_platform.is_valid() && p_export_platform->get_os_name() == "Windows";
}

void NativeRuntimeExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, "native_runtime/include_physx_libraries"), true));
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, "native_runtime/include_nvidia_dlss"), true));
}

void NativeRuntimeExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	const String editor_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const Vector<String> tags;

	if (bool(get_option("native_runtime/include_physx_libraries"))) {
		for (const char *file : PHYSX_RUNTIME_FILES) {
			String path = editor_dir.path_join(file);
			if (FileAccess::exists(path)) {
				add_shared_object(path, tags);
			}
		}
	}

	if (bool(get_option("native_runtime/include_nvidia_dlss"))) {
		if (!FileAccess::exists(editor_dir.path_join("sl.interposer.dll"))) {
			print_line("NVIDIA DLSS isn't installed next to the editor (use the editor's \"Get NVIDIA DLSS...\" button): the exported game runs without DLSS.");
			return;
		}
		for (const char *file : STREAMLINE_RUNTIME_FILES) {
			String path = editor_dir.path_join(file);
			if (FileAccess::exists(path)) {
				add_shared_object(path, tags);
			}
		}
	}
}
