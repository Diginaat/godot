/**************************************************************************/
/*  voxel_proxy_builder.h                                                 */
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

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/voxel_field_data.h"

// Builds far-field voxel proxies: area-weighted surface samples of meshes and
// MultiMesh instances are merged into a voxel grid that keeps per-cell
// coverage, colour, centroid and mean normal, then emitted as one
// camera-facing splat per occupied voxel.
class VoxelProxyBuilder : public RefCounted {
	GDCLASS(VoxelProxyBuilder, RefCounted);

	struct Sample {
		Vector3 position;
		Vector3 normal; // Unit surface normal.
		Color color; // Linear albedo.
		float area = 0.0f;
		bool modulated = false; // Material albedo comes from vertex (and instance) color.
	};

	float sample_spacing = 0.5f;
	LocalVector<Sample> samples;
	// Mesh-space samples per mesh, reused for every instance of that mesh.
	HashMap<ObjectID, LocalVector<Sample>> mesh_samples;
	HashMap<ObjectID, Color> texture_averages;
	mutable Ref<Shader> splat_shader;

	const LocalVector<Sample> &_get_mesh_samples(const Ref<Mesh> &p_mesh);
	Color _get_material_albedo(const Ref<Material> &p_material, bool &r_uses_vertex_color);
	void _append(const LocalVector<Sample> &p_local, const Transform3D &p_transform, const Color &p_modulate);

protected:
	static void _bind_methods();

public:
	void set_sample_spacing(float p_spacing);
	float get_sample_spacing() const;

	void add_mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform = Transform3D(), const Color &p_modulate = Color(1, 1, 1, 1));
	void add_multimesh(const Ref<MultiMesh> &p_multimesh, const Transform3D &p_transform = Transform3D());
	int get_sample_count() const;
	void clear();

	Ref<ArrayMesh> build(const AABB &p_bounds, int p_resolution) const;
	Ref<VoxelFieldData> build_field(const AABB &p_bounds, int p_resolution, int p_brick_resolution) const;
	Ref<Shader> get_splat_shader() const;
};
