/**************************************************************************/
/*  voxel_proxy_builder.cpp                                               */
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

#include "voxel_proxy_builder.h"

#include "core/io/image.h"
#include "core/math/math_funcs_binary.h"
#include "core/object/class_db.h"
#include "scene/resources/texture.h"

void VoxelProxyBuilder::set_sample_spacing(float p_spacing) {
	ERR_FAIL_COND(!Math::is_finite(p_spacing) || p_spacing <= 0.0f);
	if (p_spacing != sample_spacing) {
		sample_spacing = p_spacing;
		mesh_samples.clear(); // Cached mesh samples depend on the spacing.
	}
}

float VoxelProxyBuilder::get_sample_spacing() const {
	return sample_spacing;
}

Color VoxelProxyBuilder::_get_material_albedo(const Ref<Material> &p_material, bool &r_uses_vertex_color) {
	r_uses_vertex_color = false;
	const Ref<BaseMaterial3D> material = p_material;
	if (material.is_null()) {
		return Color(1, 1, 1); // Unknown shaders: neutral albedo.
	}
	r_uses_vertex_color = material->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR);
	Color albedo = material->get_albedo().srgb_to_linear();
	const Ref<Texture2D> texture = material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
	if (texture.is_valid()) {
		// A voxel spans many texels: the texture's mean colour stands in for them.
		const ObjectID id = texture->get_instance_id();
		if (!texture_averages.has(id)) {
			Color average(1, 1, 1);
			Ref<Image> image = texture->get_image();
			if (image.is_valid() && !image->is_empty()) {
				image = image->duplicate();
				if (image->is_compressed()) {
					image->decompress();
				}
				image->resize(16, 16, Image::INTERPOLATE_BILINEAR);
				Color sum(0, 0, 0, 0);
				for (int y = 0; y < 16; y++) {
					for (int x = 0; x < 16; x++) {
						const Color texel = image->get_pixel(x, y).srgb_to_linear();
						sum += texel;
					}
				}
				average = sum / 256.0f;
			}
			texture_averages[id] = average;
		}
		albedo *= texture_averages[id];
	}
	albedo.a = 1.0f;
	return albedo;
}

const LocalVector<VoxelProxyBuilder::Sample> &VoxelProxyBuilder::_get_mesh_samples(const Ref<Mesh> &p_mesh) {
	const ObjectID id = p_mesh->get_instance_id();
	if (HashMap<ObjectID, LocalVector<Sample>>::Iterator cached = mesh_samples.find(id)) {
		return cached->value;
	}
	LocalVector<Sample> &result = mesh_samples[id];
	const float spacing_squared = sample_spacing * sample_spacing;
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		if (p_mesh->surface_get_primitive_type(surface) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = p_mesh->surface_get_arrays(surface);
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		bool uses_vertex_color = false;
		const Color albedo = _get_material_albedo(p_mesh->surface_get_material(surface), uses_vertex_color);
		const bool has_normals = normals.size() == vertices.size();
		const bool has_colors = uses_vertex_color && colors.size() == vertices.size();
		const int index_count = indices.is_empty() ? vertices.size() : indices.size();
		for (int triangle = 0; triangle + 2 < index_count; triangle += 3) {
			int corner[3];
			for (int c = 0; c < 3; c++) {
				corner[c] = indices.is_empty() ? triangle + c : indices[triangle + c];
			}
			if (corner[0] < 0 || corner[1] < 0 || corner[2] < 0 || corner[0] >= vertices.size() || corner[1] >= vertices.size() || corner[2] >= vertices.size()) {
				continue;
			}
			const Vector3 p0 = vertices[corner[0]];
			const Vector3 p1 = vertices[corner[1]];
			const Vector3 p2 = vertices[corner[2]];
			const Vector3 cross = (p1 - p0).cross(p2 - p0);
			const float area = 0.5f * cross.length();
			if (!(area > 0.0f)) {
				continue;
			}
			const Vector3 face_normal = cross / (2.0f * area);
			// Split into k * k equal sub-triangles, so samples are spaced below the
			// requested spacing and each carries an equal share of the area.
			const int k = CLAMP(int(Math::ceil(Math::sqrt(area / spacing_squared))), 1, 64);
			const float share = area / float(k * k);
			for (int i = 0; i < k; i++) {
				for (int j = 0; i + j < k; j++) {
					for (int flip = 0; flip < 2; flip++) {
						if (flip == 1 && i + j + 1 >= k) {
							continue;
						}
						const float third = flip == 0 ? 1.0f / 3.0f : 2.0f / 3.0f;
						const float u = (float(i) + third) / float(k);
						const float v = (float(j) + third) / float(k);
						const float w = 1.0f - u - v;
						Sample sample;
						sample.position = p0 * w + p1 * u + p2 * v;
						if (has_normals) {
							const Vector3 n = normals[corner[0]] * w + normals[corner[1]] * u + normals[corner[2]] * v;
							sample.normal = n.length_squared() > 0.0f ? n.normalized() : face_normal;
						} else {
							sample.normal = face_normal;
						}
						sample.color = albedo;
						if (has_colors) {
							sample.color *= colors[corner[0]] * w + colors[corner[1]] * u + colors[corner[2]] * v;
							sample.color.a = 1.0f;
						}
						sample.area = share;
						sample.modulated = uses_vertex_color;
						result.push_back(sample);
					}
				}
			}
		}
	}
	return result;
}

void VoxelProxyBuilder::_append(const LocalVector<Sample> &p_local, const Transform3D &p_transform, const Color &p_modulate) {
	const Basis normal_basis = p_transform.basis.inverse().transposed();
	// Exact for uniform scale; an average for non-uniform scale.
	const float area_scale = Math::pow(Math::abs(p_transform.basis.determinant()), 2.0f / 3.0f);
	const Color modulate(p_modulate.r, p_modulate.g, p_modulate.b, 1.0f);
	samples.reserve(samples.size() + p_local.size());
	for (const Sample &local : p_local) {
		Sample sample = local;
		sample.position = p_transform.xform(local.position);
		sample.normal = normal_basis.xform(local.normal).normalized();
		sample.area = local.area * area_scale;
		if (local.modulated) {
			sample.color *= modulate;
		}
		samples.push_back(sample);
	}
}

void VoxelProxyBuilder::add_mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform, const Color &p_modulate) {
	ERR_FAIL_COND(p_mesh.is_null());
	_append(_get_mesh_samples(p_mesh), p_transform, p_modulate);
}

void VoxelProxyBuilder::add_multimesh(const Ref<MultiMesh> &p_multimesh, const Transform3D &p_transform) {
	ERR_FAIL_COND(p_multimesh.is_null());
	ERR_FAIL_COND_MSG(p_multimesh->get_transform_format() != MultiMesh::TRANSFORM_3D, "Voxel proxies need 3D MultiMesh transforms.");
	const Ref<Mesh> mesh = p_multimesh->get_mesh();
	if (mesh.is_null()) {
		return;
	}
	const LocalVector<Sample> &local = _get_mesh_samples(mesh);
	const int visible = p_multimesh->get_visible_instance_count();
	const int count = visible >= 0 ? MIN(visible, p_multimesh->get_instance_count()) : p_multimesh->get_instance_count();
	const bool colors = p_multimesh->is_using_colors();
	for (int instance = 0; instance < count; instance++) {
		_append(local, p_transform * p_multimesh->get_instance_transform(instance), colors ? p_multimesh->get_instance_color(instance) : Color(1, 1, 1));
	}
}

int VoxelProxyBuilder::get_sample_count() const {
	return samples.size();
}

void VoxelProxyBuilder::clear() {
	samples.clear();
}

Ref<Shader> VoxelProxyBuilder::get_splat_shader() const {
	if (splat_shader.is_null()) {
		splat_shader.instantiate();
		// UV holds the quad corner (-1..1), UV2.x the half size. The quad faces
		// the camera being rendered, so shadow cameras see it face on as well.
		// Splats are discs with the area of the square of that half size.
		splat_shader->set_code(R"(shader_type spatial;
render_mode cull_disabled;

void vertex() {
	vec3 offset = (INV_VIEW_MATRIX[0].xyz * UV.x + INV_VIEW_MATRIX[1].xyz * UV.y) * UV2.x * 1.1283792;
	VERTEX += inverse(mat3(MODEL_MATRIX)) * offset;
}

void fragment() {
	if (dot(UV, UV) > 1.0) {
		discard;
	}
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 1.0;
	SPECULAR = 0.2;
}
)");
	}
	return splat_shader;
}

Ref<ArrayMesh> VoxelProxyBuilder::build(const AABB &p_bounds, int p_resolution) const {
	ERR_FAIL_COND_V(p_resolution < 1 || p_resolution > 1024, Ref<ArrayMesh>());
	ERR_FAIL_COND_V(!p_bounds.is_finite() || !(p_bounds.get_longest_axis_size() > 0.0f), Ref<ArrayMesh>());
	const float voxel = p_bounds.get_longest_axis_size() / float(p_resolution);
	const Vector3i dims(MAX(1, int(Math::ceil(p_bounds.size.x / voxel))), MAX(1, int(Math::ceil(p_bounds.size.y / voxel))), MAX(1, int(Math::ceil(p_bounds.size.z / voxel))));
	struct Cell {
		double area = 0.0;
		Vector3 position;
		Vector3 normal;
		Color color = Color(0, 0, 0, 0);
	};
	// Insertion-ordered, so the output is deterministic for a given input order.
	HashMap<int64_t, Cell> cells;
	for (const Sample &sample : samples) {
		const Vector3 local = (sample.position - p_bounds.position) / voxel;
		const Vector3i cell(int(Math::floor(local.x)), int(Math::floor(local.y)), int(Math::floor(local.z)));
		if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= dims.x || cell.y >= dims.y || cell.z >= dims.z) {
			continue;
		}
		Cell &accumulated = cells[(int64_t(cell.z) * dims.y + cell.y) * dims.x + cell.x];
		accumulated.area += sample.area;
		accumulated.position += sample.position * sample.area;
		// Area-weighted mean normal; opposite faces cancel, so its length says how
		// flat (near 1) or volumetric (near 0) the voxel's surface is.
		accumulated.normal += sample.normal * sample.area;
		accumulated.color += sample.color * sample.area;
	}
	if (cells.is_empty()) {
		return Ref<ArrayMesh>();
	}
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	PackedInt32Array indices;
	vertices.resize(cells.size() * 4);
	normals.resize(cells.size() * 4);
	colors.resize(cells.size() * 4);
	uvs.resize(cells.size() * 4);
	uv2s.resize(cells.size() * 4);
	indices.resize(cells.size() * 6);
	const Vector2 corners[4] = { Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, 1) };
	// Clockwise as seen from the camera (Godot's front face): a back face would
	// flip NORMAL under cull_disabled and lose all direct light.
	const int quad[6] = { 0, 2, 1, 0, 3, 2 };
	int written = 0;
	for (const KeyValue<int64_t, Cell> &entry : cells) {
		const Cell &cell = entry.value;
		const float area = float(cell.area);
		const Vector3 centroid = cell.position / area;
		const Vector3 mean = cell.normal / area;
		const float flatness = MIN(mean.length(), 1.0f);
		// Volumetric voxels (foliage) have no dominant normal: lean them towards
		// up, which lights canopies from the sky instead of from a random side.
		const Vector3 direction = flatness > 1e-4f ? mean / flatness : Vector3(0, 1, 0);
		const Vector3 normal = (direction * flatness + Vector3(0, 1, 0) * (1.0f - flatness)).normalized();
		Color color = cell.color / area;
		color.a = 1.0f;
		// Coverage: sampled area against one voxel face. Partial cells shrink
		// their splat, so sparse foliage stays sparse at a distance.
		const float coverage = area / (voxel * voxel);
		const float half = 0.5f * voxel * Math::sqrt(MIN(coverage, 1.0f));
		for (int c = 0; c < 4; c++) {
			vertices.write[written * 4 + c] = centroid;
			normals.write[written * 4 + c] = normal;
			colors.write[written * 4 + c] = color;
			uvs.write[written * 4 + c] = corners[c];
			uv2s.write[written * 4 + c] = Vector2(half, 0);
		}
		for (int i = 0; i < 6; i++) {
			indices.write[written * 6 + i] = written * 4 + quad[i];
		}
		written++;
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	// Splats extend half a voxel around their centroids.
	mesh->set_custom_aabb(p_bounds.grow(voxel));
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(get_splat_shader());
	mesh->surface_set_material(0, material);
	return mesh;
}

Ref<VoxelFieldData> VoxelProxyBuilder::build_field(const AABB &p_bounds, int p_resolution, int p_brick_resolution) const {
	ERR_FAIL_COND_V_MSG(p_resolution < 1 || p_resolution > VoxelFieldData::MAX_RESOLUTION || !Math::is_power_of_2(uint32_t(p_resolution)), Ref<VoxelFieldData>(), "The resolution must be a power of two up to 1024.");
	ERR_FAIL_COND_V_MSG(p_brick_resolution < 1 || p_brick_resolution > p_resolution || !Math::is_power_of_2(uint32_t(p_brick_resolution)), Ref<VoxelFieldData>(), "The brick resolution must be a power of two up to the resolution.");
	ERR_FAIL_COND_V_MSG(p_resolution / p_brick_resolution > VoxelFieldData::MAX_CELLS_PER_BRICK, Ref<VoxelFieldData>(), "A brick can hold at most 64 voxels per edge: raise the brick resolution.");
	ERR_FAIL_COND_V(!p_bounds.is_finite() || !(p_bounds.get_longest_axis_size() > 0.0f), Ref<VoxelFieldData>());
	// A cube on the longest side, so every level has cubic voxels.
	const float size = p_bounds.get_longest_axis_size();
	const Vector3 origin = p_bounds.position;
	struct Accum {
		double area = 0.0;
		double position[3] = {};
		double normal[3] = {};
		double color[3] = {};
	};
	const auto key = [](uint32_t p_x, uint32_t p_y, uint32_t p_z) {
		return uint64_t(p_x) | uint64_t(p_y) << 20 | uint64_t(p_z) << 40;
	};
	// Finest level: area-weighted sums of the samples in each voxel.
	HashMap<uint64_t, Accum> cells;
	const float finest = size / float(p_resolution);
	for (const Sample &sample : samples) {
		const Vector3 local = (sample.position - origin) / finest;
		const int x = int(Math::floor(local.x));
		const int y = int(Math::floor(local.y));
		const int z = int(Math::floor(local.z));
		// Inside the bounds only, so splat centres stay inside them too.
		if (x < 0 || y < 0 || z < 0 || x >= p_resolution || y >= p_resolution || z >= p_resolution || !p_bounds.has_point(sample.position)) {
			continue;
		}
		Accum &a = cells[key(x, y, z)];
		const double area = sample.area;
		a.area += area;
		for (int i = 0; i < 3; i++) {
			a.position[i] += double(sample.position[i]) * area;
			a.normal[i] += double(sample.normal[i]) * area;
		}
		a.color[0] += double(sample.color.r) * area;
		a.color[1] += double(sample.color.g) * area;
		a.color[2] += double(sample.color.b) * area;
	}
	if (cells.is_empty()) {
		return Ref<VoxelFieldData>();
	}
	struct Entry {
		uint32_t brick = 0;
		uint32_t level = 0;
		uint64_t cell = 0;
		VoxelFieldData::Voxel voxel;
	};
	struct EntryOrder {
		_FORCE_INLINE_ bool operator()(const Entry &p_a, const Entry &p_b) const {
			if (p_a.brick != p_b.brick) {
				return p_a.brick < p_b.brick;
			}
			if (p_a.level != p_b.level) {
				return p_a.level < p_b.level;
			}
			return p_a.cell < p_b.cell;
		}
	};
	LocalVector<Entry> entries;
	// Coarser levels merge 2x2x2 cells and sum their area, so coverage stays
	// physical.
	int level = 0;
	for (int resolution = p_resolution; resolution >= p_brick_resolution; resolution >>= 1, level++) {
		const float voxel_size = size / float(resolution);
		const double face_area = double(voxel_size) * double(voxel_size);
		const uint32_t per_brick = uint32_t(resolution / p_brick_resolution);
		for (const KeyValue<uint64_t, Accum> &cell : cells) {
			const Accum &a = cell.value;
			const uint32_t x = uint32_t(cell.key & 0xFFFFF);
			const uint32_t y = uint32_t((cell.key >> 20) & 0xFFFFF);
			const uint32_t z = uint32_t((cell.key >> 40) & 0xFFFFF);
			Entry entry;
			entry.brick = (x / per_brick) | (y / per_brick) << 10 | (z / per_brick) << 20;
			entry.level = level;
			entry.cell = cell.key;
			const Vector3 mean(a.normal[0] / a.area, a.normal[1] / a.area, a.normal[2] / a.area);
			// The mean normal's length says how flat (near 1) or volumetric (near
			// 0) the voxel's surface is; volumetric voxels lean towards up.
			const float flatness = MIN(mean.length(), 1.0f);
			const Vector3 direction = flatness > 1e-4f ? mean / flatness : Vector3(0, 1, 0);
			const Vector3 normal = (direction * flatness + Vector3(0, 1, 0) * (1.0f - flatness)).normalized();
			const float coverage = float(a.area / face_area);
			const Vector3i cell_in_brick(int(x % per_brick), int(y % per_brick), int(z % per_brick));
			const Vector3 centroid(a.position[0] / a.area, a.position[1] / a.area, a.position[2] / a.area);
			const Vector3 offset = (centroid - origin) / voxel_size - Vector3(x, y, z);
			entry.voxel.cell = VoxelFieldData::encode_cell(cell_in_brick, offset);
			entry.voxel.color = VoxelFieldData::encode_color(Color(a.color[0] / a.area, a.color[1] / a.area, a.color[2] / a.area));
			entry.voxel.half_size = 0.5f * voxel_size * Math::sqrt(MIN(coverage, 1.0f));
			entry.voxel.normal = VoxelFieldData::encode_normal(normal);
			entries.push_back(entry);
		}
		if (resolution == p_brick_resolution) {
			break;
		}
		HashMap<uint64_t, Accum> parents;
		for (const KeyValue<uint64_t, Accum> &cell : cells) {
			const uint32_t x = uint32_t(cell.key & 0xFFFFF) / 2;
			const uint32_t y = uint32_t((cell.key >> 20) & 0xFFFFF) / 2;
			const uint32_t z = uint32_t((cell.key >> 40) & 0xFFFFF) / 2;
			Accum &p = parents[key(x, y, z)];
			p.area += cell.value.area;
			for (int i = 0; i < 3; i++) {
				p.position[i] += cell.value.position[i];
				p.normal[i] += cell.value.normal[i];
				p.color[i] += cell.value.color[i];
			}
		}
		cells = parents;
	}
	const int levels = level + 1;
	entries.sort_custom<EntryOrder>();
	// Every brick has voxels on every level: a merge keeps occupancy.
	const int stride = 1 + 2 * levels;
	PackedInt32Array bricks;
	PackedByteArray voxels;
	voxels.resize(int64_t(entries.size()) * sizeof(VoxelFieldData::Voxel));
	uint8_t *out = voxels.ptrw();
	for (uint32_t i = 0; i < entries.size(); i++) {
		const Entry &entry = entries[i];
		if (i == 0 || entry.brick != entries[i - 1].brick) {
			const int base = bricks.size();
			bricks.resize(base + stride);
			int32_t *b = bricks.ptrw() + base;
			b[0] = int32_t(entry.brick);
			for (int l = 0; l < levels; l++) {
				b[1 + 2 * l] = 0;
				b[2 + 2 * l] = 0;
			}
		}
		int32_t *b = bricks.ptrw() + bricks.size() - stride;
		if (b[2 + 2 * entry.level] == 0) {
			b[1 + 2 * entry.level] = int32_t(i);
		}
		b[2 + 2 * entry.level]++;
		memcpy(out + int64_t(i) * sizeof(VoxelFieldData::Voxel), &entry.voxel, sizeof(VoxelFieldData::Voxel));
	}
	Ref<VoxelFieldData> data;
	data.instantiate();
	data->set_data(p_bounds, p_resolution, p_brick_resolution, bricks, voxels);
	return data;
}

void VoxelProxyBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sample_spacing", "spacing"), &VoxelProxyBuilder::set_sample_spacing);
	ClassDB::bind_method(D_METHOD("get_sample_spacing"), &VoxelProxyBuilder::get_sample_spacing);
	ClassDB::bind_method(D_METHOD("add_mesh", "mesh", "transform", "modulate"), &VoxelProxyBuilder::add_mesh, DEFVAL(Transform3D()), DEFVAL(Color(1, 1, 1, 1)));
	ClassDB::bind_method(D_METHOD("add_multimesh", "multimesh", "transform"), &VoxelProxyBuilder::add_multimesh, DEFVAL(Transform3D()));
	ClassDB::bind_method(D_METHOD("get_sample_count"), &VoxelProxyBuilder::get_sample_count);
	ClassDB::bind_method(D_METHOD("clear"), &VoxelProxyBuilder::clear);
	ClassDB::bind_method(D_METHOD("build", "bounds", "resolution"), &VoxelProxyBuilder::build);
	ClassDB::bind_method(D_METHOD("build_field", "bounds", "resolution", "brick_resolution"), &VoxelProxyBuilder::build_field);
	ClassDB::bind_method(D_METHOD("get_splat_shader"), &VoxelProxyBuilder::get_splat_shader);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sample_spacing", PROPERTY_HINT_RANGE, "0.001,10,0.001,or_greater"), "set_sample_spacing", "get_sample_spacing");
}
