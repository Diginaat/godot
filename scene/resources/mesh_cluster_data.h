/**************************************************************************/
/*  mesh_cluster_data.h                                                   */
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

#include "scene/resources/mesh.h"

// Optional, versioned, index-only derived data. Source vertex attributes are
// owned by Mesh and never rewritten by hierarchy simplification.
class MeshClusterData : public Resource {
	GDCLASS(MeshClusterData, Resource);
	Array data;
	static PackedByteArray _surface_signature(const Array &p_arrays);
	static PackedByteArray _page_hash(const Dictionary &p_surface);

protected:
	static void _bind_methods();
	void _set_data(const Array &p_data);

public:
	using GenerateFunc = Error (*)(const Ref<Mesh> &, int, int, Array &);
	static GenerateFunc generate_func;
	static bool validate_surface(const Dictionary &p_surface);

	Error generate(const Ref<Mesh> &p_mesh, int p_cluster_triangles = 64, int p_group_size = 8);
	bool matches_mesh(const Ref<Mesh> &p_mesh) const;
	Error set_data_checked(const Array &p_data);
	Array get_data() const { return data.duplicate(true); }
	int get_surface_count() const { return data.size(); }
	Dictionary get_surface_data(int p_surface) const;
	Dictionary get_residency_state(int p_surface, const PackedByteArray &p_resident_groups) const;
	PackedInt32Array select_clusters(int p_surface, const Vector3 &p_camera_position, float p_projection_scale, float p_error_pixels) const;
	Error save_pages(const String &p_path);
	void clear_pages();
	String get_page_file() const;
};
