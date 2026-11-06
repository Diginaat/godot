/**************************************************************************/
/*  grass_field_3d.h                                                      */
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

// Render-thread state of one GrassField3D. Reference counted so that work
// queued with RenderingServer::call_on_render_thread keeps it alive after the
// node is freed; its RenderingDevice resources are freed on the render thread.
class GrassField3DGPU : public RefCounted {
	GDCLASS(GrassField3DGPU, RefCounted);

public:
	RID shader;
	RID pipeline;
	RID params;
	RID counter;
	RID heights;
	RID density;
	RID uniform_set;
	// Buffers the uniform set was built from; the MultiMesh recreates them on
	// reallocation or mesh change.
	RID instance_buffer;
	RID command_buffer;
	bool failed = false;
	bool readback_pending = false;
	SafeNumeric<uint32_t> blade_count;
	SafeNumeric<uint64_t> gpu_bytes;
};

class GrassField3D : public GeometryInstance3D {
	GDCLASS(GrassField3D, GeometryInstance3D);

public:
	// Mirrors the std140 parameter block of the generation shader.
	struct Params {
		float planes[6][4];
		float camera[4]; // Local camera position, radius.
		float grid[4]; // Grid origin x, origin z, cell size.
		float blade[4]; // Height min, height max, width, lean.
		float color_a[4];
		float color_b[4];
		int32_t grid_size[4]; // Columns, rows (grid points), has height, has density.
		int32_t patches[4]; // First patch x, first patch z, patches per side, blades per patch.
		float patch[4]; // Patch size, fade start distance.
		uint32_t misc[4]; // Max blades, seed, surface count.
	};

private:
	RID multimesh;
	Ref<GrassField3DGPU> gpu;
	Ref<Mesh> blade_mesh;
	Ref<Mesh> default_mesh;
	Ref<Material> default_material;

	float radius = 40.0f;
	float patch_size = 0.5f;
	int blades_per_patch = 16;
	int max_blades = 262144;
	float blade_height_min = 0.25f;
	float blade_height_max = 0.55f;
	float blade_width = 0.05f;
	float blade_lean = 0.35f;
	Color color_a = Color(0.16f, 0.32f, 0.07f);
	Color color_b = Color(0.34f, 0.46f, 0.12f);
	float fade_fraction = 0.4f;
	int seed = 0;

	PackedFloat32Array height_grid;
	PackedFloat32Array density_grid;
	Vector2i grid_size;
	Vector2 grid_origin;
	float grid_cell_size = 1.0f;
	float height_min = 0.0f;
	float height_max = 0.0f;
	bool grids_dirty = true;
	bool allocation_dirty = true;
	AABB aabb;

	Ref<Mesh> _get_effective_mesh();
	void _make_default_mesh();
	void _update_allocation();
	void _update_height_range();
	void _queue_generation();

	static void _render_generate(Ref<GrassField3DGPU> p_gpu, RID p_multimesh, const PackedByteArray &p_params, const PackedFloat32Array &p_heights, const PackedFloat32Array &p_density, bool p_upload);
	static void _render_free(Ref<GrassField3DGPU> p_gpu);
	static void _render_count_ready(const Vector<uint8_t> &p_data, Ref<GrassField3DGPU> p_gpu);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_blade_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_blade_mesh() const;
	void set_radius(float p_radius);
	float get_radius() const;
	void set_patch_size(float p_size);
	float get_patch_size() const;
	void set_blades_per_patch(int p_count);
	int get_blades_per_patch() const;
	void set_max_blades(int p_count);
	int get_max_blades() const;
	void set_blade_height_min(float p_height);
	float get_blade_height_min() const;
	void set_blade_height_max(float p_height);
	float get_blade_height_max() const;
	void set_blade_width(float p_width);
	float get_blade_width() const;
	void set_blade_lean(float p_lean);
	float get_blade_lean() const;
	void set_color_a(const Color &p_color);
	Color get_color_a() const;
	void set_color_b(const Color &p_color);
	Color get_color_b() const;
	void set_fade_fraction(float p_fraction);
	float get_fade_fraction() const;
	void set_seed(int p_seed);
	int get_seed() const;

	void set_height_grid(const PackedFloat32Array &p_heights);
	PackedFloat32Array get_height_grid() const;
	void set_density_grid(const PackedFloat32Array &p_density);
	PackedFloat32Array get_density_grid() const;
	void set_grid_size(const Vector2i &p_size);
	Vector2i get_grid_size() const;
	void set_grid_origin(const Vector2 &p_origin);
	Vector2 get_grid_origin() const;
	void set_grid_cell_size(float p_size);
	float get_grid_cell_size() const;

	int get_generated_blade_count() const;
	int64_t get_gpu_memory_usage() const;

	virtual AABB get_aabb() const override;
	PackedStringArray get_configuration_warnings() const override;

	GrassField3D();
	~GrassField3D();
};
