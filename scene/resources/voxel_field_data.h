/**************************************************************************/
/*  voxel_field_data.h                                                    */
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

#include "core/io/resource.h"
#include "core/math/aabb.h"

// Sparse voxel mip levels of a far field, split into bricks. The voxel grid is
// a cube on the longest side of `bounds`, at its position; level 0 has
// `resolution` voxels along each edge of the cube; every coarser level halves
// it down to `brick_resolution`, where a brick holds one voxel. Each brick
// lists its voxels per level, so a renderer can pick one level per brick
// without gaps or overlaps between bricks.
class VoxelFieldData : public Resource {
	GDCLASS(VoxelFieldData, Resource);
	RES_BASE_EXTENSION("voxfield");

public:
	// One voxel as stored in `voxels` and read by the GPU (16-byte
	// layout): cell inside its brick (6 bits per axis) and the surface centroid
	// inside the cell (4 bits per axis); sRGB albedo; an octahedral normal in
	// two signed 16-bit values; the splat half size.
	struct Voxel {
		uint32_t cell;
		uint32_t color;
		uint32_t normal;
		float half_size;
	};
	static_assert(sizeof(Voxel) == 16);
	// Cells per brick edge on the finest level are limited by the 6-bit cell.
	static constexpr int MAX_CELLS_PER_BRICK = 64;

	static constexpr int MAX_RESOLUTION = 1024;

	static uint32_t encode_normal(const Vector3 &p_normal);
	static Vector3 decode_normal(uint32_t p_packed);
	static uint32_t encode_cell(const Vector3i &p_cell, const Vector3 &p_offset);
	static uint32_t encode_color(const Color &p_linear);

private:
	AABB bounds = AABB(Vector3(), Vector3(1, 1, 1));
	int resolution = 1;
	int brick_resolution = 1;
	// Per brick: packed brick coordinates (10 bits per axis), then the first
	// voxel and the voxel count of each level.
	PackedInt32Array bricks;
	PackedByteArray voxels;
	bool valid = false;

	void _validate();
	void _locate(int p_index, int &r_brick, int &r_level) const;

protected:
	static void _bind_methods();

public:
	void set_bounds(const AABB &p_bounds);
	AABB get_bounds() const;
	Vector3 get_origin() const { return bounds.position; }
	float get_size() const { return bounds.get_longest_axis_size(); }
	void set_resolution(int p_resolution);
	int get_resolution() const;
	void set_brick_resolution(int p_resolution);
	int get_brick_resolution() const;
	void set_bricks(const PackedInt32Array &p_bricks);
	PackedInt32Array get_bricks() const;
	void set_voxels(const PackedByteArray &p_voxels);
	PackedByteArray get_voxels() const;

	void set_data(const AABB &p_bounds, int p_resolution, int p_brick_resolution, const PackedInt32Array &p_bricks, const PackedByteArray &p_voxels);

	bool is_valid() const { return valid; }
	int get_level_count() const;
	int get_brick_count() const;
	int get_voxel_count() const;
	int get_level_voxel_count(int p_level) const;
	// The most splats one selection can produce: per brick, its largest level.
	int get_max_splat_count() const;
	float get_level_voxel_size(int p_level) const;
	AABB get_aabb() const;
	Dictionary get_voxel(int p_index) const;
	// Bounding sphere of a brick, splats included.
	void get_brick_sphere(int p_brick, Vector3 &r_center, float &r_radius) const;

	// The level the GPU picks for a brick: the coarsest level whose voxels are
	// at most `voxel_pixels` wide at the brick's nearest point, or level 0 when
	// even that is wider. Positions are in the field's space; `pixel_scale` is
	// pixels per unit at distance 1 (perspective) or pixels per unit
	// (orthogonal).
	int select_level(int p_brick, const Vector3 &p_camera, float p_pixel_scale, bool p_orthogonal, float p_voxel_pixels) const;
	int count_splats(const Vector3 &p_camera, float p_pixel_scale, bool p_orthogonal, float p_voxel_pixels) const;
};
