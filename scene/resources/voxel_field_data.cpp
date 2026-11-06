/**************************************************************************/
/*  voxel_field_data.cpp                                                  */
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

#include "voxel_field_data.h"

#include "core/math/math_funcs_binary.h"
#include "core/object/class_db.h"

uint32_t VoxelFieldData::encode_normal(const Vector3 &p_normal) {
	Vector3 n = p_normal.length_squared() > 0.0f ? p_normal.normalized() : Vector3(0, 1, 0);
	const float l1 = Math::abs(n.x) + Math::abs(n.y) + Math::abs(n.z);
	float x = n.x / l1;
	float y = n.y / l1;
	if (n.z < 0.0f) {
		const float ox = x;
		x = (1.0f - Math::abs(y)) * (ox >= 0.0f ? 1.0f : -1.0f);
		y = (1.0f - Math::abs(ox)) * (y >= 0.0f ? 1.0f : -1.0f);
	}
	const uint32_t qx = uint32_t(int32_t(Math::round(CLAMP(x, -1.0f, 1.0f) * 32767.0f))) & 0xFFFFu;
	const uint32_t qy = uint32_t(int32_t(Math::round(CLAMP(y, -1.0f, 1.0f) * 32767.0f))) & 0xFFFFu;
	return qx | (qy << 16);
}

Vector3 VoxelFieldData::decode_normal(uint32_t p_packed) {
	const float x = float(int16_t(p_packed & 0xFFFFu)) / 32767.0f;
	const float y = float(int16_t(p_packed >> 16)) / 32767.0f;
	Vector3 n(x, y, 1.0f - Math::abs(x) - Math::abs(y));
	if (n.z < 0.0f) {
		n.x = (1.0f - Math::abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
		n.y = (1.0f - Math::abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
	}
	return n.normalized();
}

uint32_t VoxelFieldData::encode_cell(const Vector3i &p_cell, const Vector3 &p_offset) {
	// Offsets in 1/16 cells, decoded at the centre of their step: at most 1/32
	// of a voxel off.
	uint32_t packed = 0;
	for (int i = 0; i < 3; i++) {
		const uint32_t offset = uint32_t(CLAMP(int(Math::round(p_offset[i] * 16.0f - 0.5f)), 0, 15));
		packed |= (uint32_t(p_cell[i]) & 63u) << (6 * i);
		packed |= offset << (18 + 4 * i);
	}
	return packed;
}

uint32_t VoxelFieldData::encode_color(const Color &p_linear) {
	// sRGB keeps dark foliage tones apart in 8 bits.
	const Color c = p_linear.clamp().linear_to_srgb();
	return uint32_t(Math::round(c.r * 255.0f)) | uint32_t(Math::round(c.g * 255.0f)) << 8 | uint32_t(Math::round(c.b * 255.0f)) << 16 | 0xFF000000u;
}

void VoxelFieldData::_validate() {
	valid = false;
	if (!bounds.is_finite() || !(bounds.get_longest_axis_size() > 0.0f) || bounds.size.x < 0.0f || bounds.size.y < 0.0f || bounds.size.z < 0.0f) {
		return;
	}
	if (resolution < 1 || resolution > MAX_RESOLUTION || !Math::is_power_of_2(uint32_t(resolution))) {
		return;
	}
	if (brick_resolution < 1 || brick_resolution > resolution || !Math::is_power_of_2(uint32_t(brick_resolution)) || resolution / brick_resolution > MAX_CELLS_PER_BRICK) {
		return;
	}
	if (voxels.size() % int(sizeof(Voxel)) != 0) {
		return;
	}
	const int levels = get_level_count();
	const int stride = 1 + 2 * levels;
	if (bricks.size() % stride != 0) {
		return;
	}
	const int64_t voxel_count = voxels.size() / int(sizeof(Voxel));
	const int32_t *b = bricks.ptr();
	for (int brick = 0; brick < bricks.size() / stride; brick++) {
		const uint32_t packed = uint32_t(b[brick * stride]);
		if ((packed >> 30) != 0 || int(packed & 1023u) >= brick_resolution || int((packed >> 10) & 1023u) >= brick_resolution || int((packed >> 20) & 1023u) >= brick_resolution) {
			return;
		}
		for (int level = 0; level < levels; level++) {
			const int64_t first = b[brick * stride + 1 + 2 * level];
			const int64_t count = b[brick * stride + 2 + 2 * level];
			if (first < 0 || count < 0 || first + count > voxel_count) {
				return;
			}
		}
	}
	valid = true;
}

void VoxelFieldData::set_bounds(const AABB &p_bounds) {
	bounds = p_bounds;
	_validate();
	emit_changed();
}

AABB VoxelFieldData::get_bounds() const {
	return bounds;
}

void VoxelFieldData::set_resolution(int p_resolution) {
	resolution = p_resolution;
	_validate();
	emit_changed();
}

int VoxelFieldData::get_resolution() const {
	return resolution;
}

void VoxelFieldData::set_brick_resolution(int p_resolution) {
	brick_resolution = p_resolution;
	_validate();
	emit_changed();
}

int VoxelFieldData::get_brick_resolution() const {
	return brick_resolution;
}

void VoxelFieldData::set_bricks(const PackedInt32Array &p_bricks) {
	bricks = p_bricks;
	_validate();
	emit_changed();
}

PackedInt32Array VoxelFieldData::get_bricks() const {
	return bricks;
}

void VoxelFieldData::set_voxels(const PackedByteArray &p_voxels) {
	voxels = p_voxels;
	_validate();
	emit_changed();
}

PackedByteArray VoxelFieldData::get_voxels() const {
	return voxels;
}

void VoxelFieldData::set_data(const AABB &p_bounds, int p_resolution, int p_brick_resolution, const PackedInt32Array &p_bricks, const PackedByteArray &p_voxels) {
	bounds = p_bounds;
	resolution = p_resolution;
	brick_resolution = p_brick_resolution;
	bricks = p_bricks;
	voxels = p_voxels;
	_validate();
	emit_changed();
}

int VoxelFieldData::get_level_count() const {
	if (resolution < 1 || brick_resolution < 1 || brick_resolution > resolution) {
		return 0;
	}
	int levels = 1;
	for (int r = resolution; r > brick_resolution; r >>= 1) {
		levels++;
	}
	return levels;
}

int VoxelFieldData::get_brick_count() const {
	return valid ? bricks.size() / (1 + 2 * get_level_count()) : 0;
}

int VoxelFieldData::get_voxel_count() const {
	return valid ? voxels.size() / int(sizeof(Voxel)) : 0;
}

int VoxelFieldData::get_level_voxel_count(int p_level) const {
	ERR_FAIL_COND_V(!valid, 0);
	const int levels = get_level_count();
	ERR_FAIL_INDEX_V(p_level, levels, 0);
	const int stride = 1 + 2 * levels;
	int count = 0;
	for (int brick = 0; brick < get_brick_count(); brick++) {
		count += bricks[brick * stride + 2 + 2 * p_level];
	}
	return count;
}

int VoxelFieldData::get_max_splat_count() const {
	if (!valid) {
		return 0;
	}
	const int levels = get_level_count();
	const int stride = 1 + 2 * levels;
	int64_t count = 0;
	for (int brick = 0; brick < get_brick_count(); brick++) {
		int most = 0;
		for (int level = 0; level < levels; level++) {
			most = MAX(most, bricks[brick * stride + 2 + 2 * level]);
		}
		count += most;
	}
	return int(MIN(count, int64_t(INT32_MAX)));
}

float VoxelFieldData::get_level_voxel_size(int p_level) const {
	ERR_FAIL_INDEX_V(p_level, get_level_count(), 0.0f);
	return get_size() / float(resolution >> p_level);
}

AABB VoxelFieldData::get_aabb() const {
	if (!valid) {
		return AABB();
	}
	// Centroids lie inside the bounds; a camera-facing splat reaches sqrt(2)
	// half sizes, at most half a coarsest voxel each, beyond its centroid.
	const float margin = 0.7072f * get_size() / float(brick_resolution);
	return bounds.grow(margin);
}

void VoxelFieldData::_locate(int p_index, int &r_brick, int &r_level) const {
	const int levels = get_level_count();
	const int stride = 1 + 2 * levels;
	for (int brick = 0; brick < get_brick_count(); brick++) {
		for (int level = 0; level < levels; level++) {
			const int first = bricks[brick * stride + 1 + 2 * level];
			if (p_index >= first && p_index < first + bricks[brick * stride + 2 + 2 * level]) {
				r_brick = brick;
				r_level = level;
				return;
			}
		}
	}
	r_brick = -1;
	r_level = -1;
}

Dictionary VoxelFieldData::get_voxel(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, get_voxel_count(), Dictionary());
	Voxel voxel;
	memcpy(&voxel, voxels.ptr() + int64_t(p_index) * sizeof(Voxel), sizeof(Voxel));
	int brick = -1;
	int level = -1;
	_locate(p_index, brick, level);
	ERR_FAIL_COND_V_MSG(brick < 0, Dictionary(), "The voxel belongs to no brick.");
	// The same decoding as the selection shader.
	const uint32_t packed = uint32_t(bricks[brick * (1 + 2 * get_level_count())]);
	const float brick_size = get_size() / float(brick_resolution);
	const float voxel_size = get_size() / float(resolution >> level);
	Vector3 position;
	for (int i = 0; i < 3; i++) {
		const float brick_cell = float((packed >> (10 * i)) & 1023u);
		const float cell = float((voxel.cell >> (6 * i)) & 63u);
		const float offset = (float((voxel.cell >> (18 + 4 * i)) & 15u) + 0.5f) / 16.0f;
		position[i] = bounds.position[i] + brick_cell * brick_size + (cell + offset) * voxel_size;
	}
	Dictionary result;
	result["position"] = position;
	result["half_size"] = voxel.half_size;
	result["color"] = Color(float(voxel.color & 255u) / 255.0f, float((voxel.color >> 8) & 255u) / 255.0f, float((voxel.color >> 16) & 255u) / 255.0f).srgb_to_linear();
	result["normal"] = decode_normal(voxel.normal);
	result["level"] = level;
	result["brick"] = brick;
	return result;
}

void VoxelFieldData::get_brick_sphere(int p_brick, Vector3 &r_center, float &r_radius) const {
	const int stride = 1 + 2 * get_level_count();
	const uint32_t packed = uint32_t(bricks[p_brick * stride]);
	const float brick_size = get_size() / float(brick_resolution);
	const Vector3 cell(float(packed & 1023u), float((packed >> 10) & 1023u), float((packed >> 20) & 1023u));
	r_center = bounds.position + (cell + Vector3(0.5f, 0.5f, 0.5f)) * brick_size;
	// Half the brick diagonal, plus half a coarsest voxel for the splats.
	r_radius = brick_size * 1.3660254f;
}

int VoxelFieldData::select_level(int p_brick, const Vector3 &p_camera, float p_pixel_scale, bool p_orthogonal, float p_voxel_pixels) const {
	ERR_FAIL_INDEX_V(p_brick, get_brick_count(), -1);
	Vector3 center;
	float radius = 0.0f;
	get_brick_sphere(p_brick, center, radius);
	float pixels_per_unit = p_pixel_scale;
	if (!p_orthogonal) {
		const float nearest = MAX((center - p_camera).length() - radius, 1e-4f);
		pixels_per_unit = p_pixel_scale / nearest;
	}
	int selected = 0;
	for (int level = 0; level < get_level_count(); level++) {
		const float voxel_size = get_size() / float(resolution >> level);
		if (voxel_size * pixels_per_unit > p_voxel_pixels) {
			break;
		}
		selected = level;
	}
	return selected;
}

int VoxelFieldData::count_splats(const Vector3 &p_camera, float p_pixel_scale, bool p_orthogonal, float p_voxel_pixels) const {
	ERR_FAIL_COND_V(!valid, 0);
	const int stride = 1 + 2 * get_level_count();
	int count = 0;
	for (int brick = 0; brick < get_brick_count(); brick++) {
		const int level = select_level(brick, p_camera, p_pixel_scale, p_orthogonal, p_voxel_pixels);
		count += bricks[brick * stride + 2 + 2 * level];
	}
	return count;
}

void VoxelFieldData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bounds", "bounds"), &VoxelFieldData::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &VoxelFieldData::get_bounds);
	ClassDB::bind_method(D_METHOD("set_resolution", "resolution"), &VoxelFieldData::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &VoxelFieldData::get_resolution);
	ClassDB::bind_method(D_METHOD("set_brick_resolution", "resolution"), &VoxelFieldData::set_brick_resolution);
	ClassDB::bind_method(D_METHOD("get_brick_resolution"), &VoxelFieldData::get_brick_resolution);
	ClassDB::bind_method(D_METHOD("set_bricks", "bricks"), &VoxelFieldData::set_bricks);
	ClassDB::bind_method(D_METHOD("get_bricks"), &VoxelFieldData::get_bricks);
	ClassDB::bind_method(D_METHOD("set_voxels", "voxels"), &VoxelFieldData::set_voxels);
	ClassDB::bind_method(D_METHOD("get_voxels"), &VoxelFieldData::get_voxels);
	ClassDB::bind_method(D_METHOD("is_valid"), &VoxelFieldData::is_valid);
	ClassDB::bind_method(D_METHOD("get_level_count"), &VoxelFieldData::get_level_count);
	ClassDB::bind_method(D_METHOD("get_brick_count"), &VoxelFieldData::get_brick_count);
	ClassDB::bind_method(D_METHOD("get_voxel_count"), &VoxelFieldData::get_voxel_count);
	ClassDB::bind_method(D_METHOD("get_level_voxel_count", "level"), &VoxelFieldData::get_level_voxel_count);
	ClassDB::bind_method(D_METHOD("get_max_splat_count"), &VoxelFieldData::get_max_splat_count);
	ClassDB::bind_method(D_METHOD("get_level_voxel_size", "level"), &VoxelFieldData::get_level_voxel_size);
	ClassDB::bind_method(D_METHOD("get_aabb"), &VoxelFieldData::get_aabb);
	ClassDB::bind_method(D_METHOD("get_voxel", "index"), &VoxelFieldData::get_voxel);
	ClassDB::bind_method(D_METHOD("select_level", "brick", "camera", "pixel_scale", "orthogonal", "voxel_pixels"), &VoxelFieldData::select_level);
	ClassDB::bind_method(D_METHOD("count_splats", "camera", "pixel_scale", "orthogonal", "voxel_pixels"), &VoxelFieldData::count_splats);

	ADD_PROPERTY(PropertyInfo(Variant::AABB, "bounds", PROPERTY_HINT_NONE, "suffix:m"), "set_bounds", "get_bounds");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "brick_resolution"), "set_brick_resolution", "get_brick_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "bricks", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_bricks", "get_bricks");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "voxels", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_voxels", "get_voxels");
}
