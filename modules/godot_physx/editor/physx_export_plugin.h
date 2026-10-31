/**************************************************************************/
/*  physx_export_plugin.h                                                 */
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

// Ships the module's shared libraries with an exported Windows / Linux game.
// The export copies only the template binary and the .pck, but a build with
// blast_sdk= links NvBlast* at load time (the game won't start without them),
// one with physx_gpu=yes loads PhysXGpu_64 at run time (missing: silent CPU
// fallback), and one with flow_sdk= loads nvflow / nvflowext (missing: the
// Flow nodes stay inert). The libraries are taken from the folder of the
// export template the preset uses -- the custom template if set, else the
// editor's templates folder for this version -- so a Windows editor exporting
// with a Linux template folder ships the .so files. Also ships the PhysX
// BSD-3-Clause license text the redistributed binaries must carry.
class PhysXExportPlugin : public EditorExportPlugin {
	GDCLASS(PhysXExportPlugin, EditorExportPlugin);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;

public:
	virtual String get_name() const override { return "PhysX"; }
};
