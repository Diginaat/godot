/**************************************************************************/
/*  voxel_field_3d.h                                                      */
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

#include "core/templates/safe_refcount.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/voxel_field_data.h"

// Render-thread state of one VoxelField3D, kept alive by queued render-thread
// work after the node is freed (see GrassField3DGPU).
class VoxelField3DGPU : public RefCounted {
	GDCLASS(VoxelField3DGPU, RefCounted);

public:
	RID shader;
	RID pipeline;
	RID params;
	RID counter;
	RID bricks;
	RID voxels;
	RID uniform_set;
	RID instance_buffer;
	RID command_buffer;
	uint32_t brick_count = 0;
	bool failed = false;
	bool readback_pending = false;
	SafeNumeric<uint32_t> splat_count;
	SafeNumeric<uint64_t> gpu_bytes;
};

class VoxelField3D : public GeometryInstance3D {
	GDCLASS(VoxelField3D, GeometryInstance3D);

public:
	// Mirrors the std140 parameter block of the selection shader.
	struct Params {
		float planes[6][4];
		float camera[4]; // Local camera position, pixel scale.
		float field[4]; // Origin, size.
		float selection[4]; // Voxel pixels, orthogonal, frustum culling, in range.
		uint32_t info[4]; // Brick count, level count, brick resolution, resolution.
		uint32_t misc[4]; // Max splats, surface count, dispatch width.
	};

private:
	RID multimesh;
	Ref<VoxelField3DGPU> gpu;
	Ref<VoxelFieldData> data;
	Ref<Mesh> splat_mesh;
	Ref<Material> splat_material;

	float voxel_pixels = 2.0f;
	int max_splats = 0; // 0: as many as the field can select.
	int capacity = 0; // Allocated instances.
	bool frustum_culling = true;
	bool data_dirty = true;
	bool selection_empty = false;

	void _make_splat_mesh();
	int _get_wanted_capacity() const;
	void _data_changed();
	void _queue_selection();

	static void _render_select(Ref<VoxelField3DGPU> p_gpu, RID p_multimesh, const PackedByteArray &p_params, const PackedInt32Array &p_bricks, const PackedByteArray &p_voxels, bool p_upload);
	static void _render_free(Ref<VoxelField3DGPU> p_gpu);
	static void _render_count_ready(const Vector<uint8_t> &p_data, Ref<VoxelField3DGPU> p_gpu);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_data(const Ref<VoxelFieldData> &p_data);
	Ref<VoxelFieldData> get_data() const;
	void set_voxel_pixels(float p_pixels);
	float get_voxel_pixels() const;
	void set_max_splats(int p_count);
	int get_max_splats() const;
	void set_frustum_culling(bool p_enabled);
	bool is_frustum_culling_enabled() const;

	int get_splat_count() const;
	int64_t get_gpu_memory_usage() const;
	Ref<Material> get_splat_material() const;

	virtual AABB get_aabb() const override;
	PackedStringArray get_configuration_warnings() const override;

	VoxelField3D();
	~VoxelField3D();
};
