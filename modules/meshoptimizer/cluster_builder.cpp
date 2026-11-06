/**************************************************************************/
/*  cluster_builder.cpp                                                   */
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

#include "cluster_builder.h"

#include <thirdparty/meshoptimizer/meshoptimizer.h>

#include <cfloat>

namespace {

struct Group {
	AABB bounds;
	float error = 0;
	int first_cluster = 0;
	int cluster_count = 0;
	int first_child = 0;
	int child_count = 0;
	int parent = -1;
	Vector<uint32_t> indices;
};

struct Cooker {
	PackedVector3Array vertices;
	Vector<float> positions;
	Vector<float> attributes;
	int attribute_count = 0;
	int max_triangles = 64;
	Vector<Group> groups;
	PackedInt32Array clusters;
	PackedVector3Array cluster_bounds;
	PackedInt32Array indices;
	PackedInt32Array children;

	Vector<Vector<uint32_t>> split(const Vector<uint32_t> &p_indices) const {
		const size_t capacity = meshopt_buildMeshletsBound(p_indices.size(), 64, max_triangles);
		Vector<meshopt_Meshlet> meshlets;
		Vector<uint32_t> meshlet_vertices;
		Vector<uint8_t> triangles;
		meshlets.resize(capacity);
		meshlet_vertices.resize(capacity * 64);
		triangles.resize(capacity * max_triangles * 3);
		const size_t count = meshopt_buildMeshlets(meshlets.ptrw(), meshlet_vertices.ptrw(), triangles.ptrw(), p_indices.ptr(), p_indices.size(), positions.ptr(), vertices.size(), 3 * sizeof(float), 64, max_triangles, 0);
		Vector<Vector<uint32_t>> result;
		result.resize(count);
		for (size_t m = 0; m < count; m++) {
			const meshopt_Meshlet &meshlet = meshlets[m];
			Vector<uint32_t> &output = result.write[m];
			output.resize(meshlet.triangle_count * 3);
			for (size_t t = 0; t < meshlet.triangle_count * 3; t++) {
				output.write[t] = meshlet_vertices[meshlet.vertex_offset + triangles[meshlet.triangle_offset + t]];
			}
		}
		return result;
	}

	AABB bounds_of(const Vector<uint32_t> &p_indices) const {
		AABB bounds(vertices[p_indices[0]], Vector3());
		for (uint32_t index : p_indices) {
			bounds.expand_to(vertices[index]);
		}
		return bounds;
	}

	int append_group(const Vector<uint32_t> &p_indices, const Vector<int> &p_children, float p_error) {
		const int id = groups.size();
		Group group;
		group.indices = p_indices;
		group.bounds = bounds_of(p_indices);
		group.error = p_error;
		group.first_child = children.size();
		group.child_count = p_children.size();
		for (int child : p_children) {
			group.bounds.merge_with(groups[child].bounds);
			groups.write[child].parent = id;
			children.push_back(child);
		}
		group.first_cluster = clusters.size() / 3;
		const Vector<Vector<uint32_t>> pieces = split(p_indices);
		group.cluster_count = pieces.size();
		for (const Vector<uint32_t> &piece : pieces) {
			clusters.push_back(indices.size());
			clusters.push_back(piece.size());
			clusters.push_back(id);
			const AABB box = bounds_of(piece);
			cluster_bounds.push_back(box.position);
			cluster_bounds.push_back(box.size);
			for (uint32_t index : piece) {
				indices.push_back(index);
			}
		}
		groups.push_back(group);
		return id;
	}

	Dictionary build(const Vector<uint32_t> &p_source, int p_group_size) {
		Vector<int> level;
		for (const Vector<uint32_t> &piece : split(p_source)) {
			level.push_back(append_group(piece, Vector<int>(), 0));
		}
		while (level.size() > 1) {
			Vector<uint32_t> partition;
			Vector<uint32_t> level_indices;
			Vector<uint32_t> counts;
			partition.resize(level.size());
			for (int group : level) {
				counts.push_back(groups[group].indices.size());
				level_indices.append_array(groups[group].indices);
			}
			size_t partition_count = meshopt_partitionClusters(partition.ptrw(), level_indices.ptr(), level_indices.size(), counts.ptr(), counts.size(), positions.ptr(), vertices.size(), 3 * sizeof(float), p_group_size);
			// Guarantee progress even for disconnected/topologically unsimplifiable
			// content. Keeping its triangles is preferable to creating holes.
			if (partition_count >= size_t(level.size())) {
				partition_count = (level.size() + p_group_size - 1) / p_group_size;
				for (int i = 0; i < level.size(); i++) {
					partition.write[i] = i / p_group_size;
				}
			}
			Vector<Vector<int>> buckets;
			buckets.resize(partition_count);
			for (int i = 0; i < level.size(); i++) {
				buckets.write[partition[i]].push_back(level[i]);
			}
			Vector<int> next;
			for (const Vector<int> &bucket : buckets) {
				if (bucket.is_empty()) {
					continue;
				}
				if (bucket.size() == 1) {
					next.push_back(bucket[0]);
					continue;
				}
				Vector<uint32_t> source;
				float child_error = 0;
				for (int child : bucket) {
					source.append_array(groups[child].indices);
					child_error = MAX(child_error, groups[child].error);
				}
				Vector<uint32_t> simplified;
				simplified.resize(source.size());
				Vector<float> weights;
				weights.resize(attribute_count);
				weights.fill(1.0f);
				float error = 0;
				const size_t count = meshopt_simplifyWithAttributes(simplified.ptrw(), source.ptr(), source.size(), positions.ptr(), vertices.size(), 3 * sizeof(float), attributes.ptr(), attribute_count * sizeof(float), weights.ptr(), attribute_count, nullptr, MAX(3, (source.size() / 6) * 3), FLT_MAX, meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute | meshopt_SimplifySparse, &error);
				if (count == 0 || !Math::is_finite(error)) {
					simplified = source;
					error = 0;
				} else {
					simplified.resize(count);
				}
				next.push_back(append_group(simplified, bucket, child_error + error));
			}
			level = next;
		}
		PackedInt32Array records;
		PackedVector3Array bounds;
		PackedFloat32Array errors;
		for (const Group &group : groups) {
			records.append_array(PackedInt32Array({ group.first_cluster, group.cluster_count, group.first_child, group.child_count, group.parent }));
			bounds.push_back(group.bounds.position);
			bounds.push_back(group.bounds.size);
			errors.push_back(group.error);
		}
		Dictionary result;
		result["version"] = 1;
		result["vertex_count"] = vertices.size();
		result["source_index_count"] = p_source.size();
		result["cluster_triangles"] = max_triangles;
		result["groups"] = records;
		result["bounds"] = bounds;
		result["errors"] = errors;
		result["clusters"] = clusters;
		result["cluster_bounds"] = cluster_bounds;
		result["indices"] = indices;
		result["children"] = children;
		result["roots"] = PackedInt32Array({ level[0] });
		return result;
	}
};

} // namespace

Error generate_mesh_clusters(const Ref<Mesh> &p_mesh, int p_cluster_triangles, int p_group_size, Array &r_data) {
	if (p_mesh->get_blend_shape_count() != 0 || p_mesh->get_surface_count() == 0 || p_mesh->get_surface_count() > 256) {
		return ERR_UNAVAILABLE;
	}
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		if (p_mesh->surface_get_primitive_type(surface) != Mesh::PRIMITIVE_TRIANGLES) {
			return ERR_UNAVAILABLE;
		}
		const Array arrays = p_mesh->surface_get_arrays(surface);
		if (arrays.size() != Mesh::ARRAY_MAX || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
			return ERR_INVALID_DATA;
		}
		for (int unsupported : { Mesh::ARRAY_BONES, Mesh::ARRAY_WEIGHTS, Mesh::ARRAY_CUSTOM0, Mesh::ARRAY_CUSTOM1, Mesh::ARRAY_CUSTOM2, Mesh::ARRAY_CUSTOM3 }) {
			if (arrays[unsupported].get_type() != Variant::NIL) {
				return ERR_UNAVAILABLE;
			}
		}
		Cooker cooker;
		cooker.max_triangles = p_cluster_triangles;
		cooker.vertices = arrays[Mesh::ARRAY_VERTEX];
		if (cooker.vertices.is_empty()) {
			return ERR_INVALID_DATA;
		}
		cooker.positions.resize(cooker.vertices.size() * 3);
		for (int i = 0; i < cooker.vertices.size(); i++) {
			for (int axis = 0; axis < 3; axis++) {
				const float value = cooker.vertices[i][axis];
				if (!Math::is_finite(value)) {
					return ERR_INVALID_DATA;
				}
				cooker.positions.write[i * 3 + axis] = value;
			}
		}
		// Attribute metrics only choose existing vertex indices; original normals,
		// tangents, UVs and colors remain byte-for-byte in their Mesh buffers.
		const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
		const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		const PackedVector2Array uv = arrays[Mesh::ARRAY_TEX_UV];
		const PackedVector2Array uv2 = arrays[Mesh::ARRAY_TEX_UV2];
		const int n = cooker.vertices.size();
		if ((!normals.is_empty() && normals.size() != n) || (!tangents.is_empty() && tangents.size() != n * 4) ||
				(!colors.is_empty() && colors.size() != n) || (!uv.is_empty() && uv.size() != n) || (!uv2.is_empty() && uv2.size() != n)) {
			return ERR_INVALID_DATA;
		}
		cooker.attribute_count = (normals.is_empty() ? 0 : 3) + (tangents.is_empty() ? 0 : 4) + (colors.is_empty() ? 0 : 4) + (uv.is_empty() ? 0 : 2) + (uv2.is_empty() ? 0 : 2);
		for (int i = 0; i < n; i++) {
			if (!normals.is_empty()) {
				for (int a = 0; a < 3; a++) {
					cooker.attributes.push_back(normals[i][a]);
				}
			}
			if (!tangents.is_empty()) {
				for (int a = 0; a < 4; a++) {
					cooker.attributes.push_back(tangents[i * 4 + a]);
				}
			}
			if (!colors.is_empty()) {
				for (int a = 0; a < 4; a++) {
					cooker.attributes.push_back(colors[i][a]);
				}
			}
			for (const PackedVector2Array &coordinates : { uv, uv2 }) {
				if (!coordinates.is_empty()) {
					cooker.attributes.push_back(coordinates[i].x);
					cooker.attributes.push_back(coordinates[i].y);
				}
			}
		}
		for (float value : cooker.attributes) {
			if (!Math::is_finite(value)) {
				return ERR_INVALID_DATA;
			}
		}
		PackedInt32Array source_indices = arrays[Mesh::ARRAY_INDEX];
		if (source_indices.is_empty()) {
			source_indices.resize(n);
			for (int i = 0; i < n; i++) {
				source_indices.write[i] = i;
			}
		}
		if (source_indices.size() % 3) {
			return ERR_INVALID_DATA;
		}
		Vector<uint32_t> source;
		source.resize(source_indices.size());
		for (int i = 0; i < source_indices.size(); i++) {
			if (source_indices[i] < 0 || source_indices[i] >= n) {
				return ERR_INVALID_DATA;
			}
			source.write[i] = source_indices[i];
		}
		r_data.push_back(cooker.build(source, p_group_size));
	}
	return OK;
}
