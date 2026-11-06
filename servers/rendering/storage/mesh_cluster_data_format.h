/**************************************************************************/
/*  mesh_cluster_data_format.h                                            */
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

#ifndef MESH_CLUSTER_DATA_FORMAT_H
#define MESH_CLUSTER_DATA_FORMAT_H

#include "core/math/aabb.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace MeshClusterDataFormat {

// Page file (.clodpages), little-endian: magic, version and surface count, then
// one PAGE_SURFACE_BYTES entry per surface (SHA-256 of the surface's indices,
// groups and clusters; index count; reserved; byte offset of its indices),
// then each surface's indices as uint32, in the record's group order.
static constexpr uint32_t PAGE_MAGIC = 0x504c4347; // "GCLP"
static constexpr uint32_t PAGE_VERSION = 1;
static constexpr uint32_t PAGE_HEADER_BYTES = 12;
static constexpr uint32_t PAGE_SURFACE_BYTES = 48;

inline bool validate_surface(const Dictionary &p_surface) {
	if (int(p_surface.get("version", 0)) != 1 || int(p_surface.get("vertex_count", 0)) <= 0 || int(p_surface.get("source_index_count", 0)) <= 0) {
		return false;
	}
	const char *int_arrays[] = { "groups", "clusters", "indices", "children", "roots" };
	for (const char *key : int_arrays) {
		if (p_surface.get(key, Variant()).get_type() != Variant::PACKED_INT32_ARRAY) {
			return false;
		}
	}
	if (p_surface.get("bounds", Variant()).get_type() != Variant::PACKED_VECTOR3_ARRAY ||
			p_surface.get("cluster_bounds", Variant()).get_type() != Variant::PACKED_VECTOR3_ARRAY ||
			p_surface.get("errors", Variant()).get_type() != Variant::PACKED_FLOAT32_ARRAY) {
		return false;
	}
	// Optional page file written by MeshClusterData::save_pages(); all three or none.
	const bool page_file = p_surface.has("page_file");
	if (page_file != p_surface.has("page_hash") || page_file != p_surface.has("page_surface") ||
			(page_file && (p_surface["page_file"].get_type() != Variant::STRING || p_surface["page_hash"].get_type() != Variant::PACKED_BYTE_ARRAY ||
								  PackedByteArray(p_surface["page_hash"]).size() != 32 || p_surface["page_surface"].get_type() != Variant::INT || int(p_surface["page_surface"]) < 0))) {
		return false;
	}
	const PackedInt32Array groups = p_surface["groups"];
	const PackedInt32Array clusters = p_surface["clusters"];
	const PackedInt32Array indices = p_surface["indices"];
	const PackedInt32Array children = p_surface["children"];
	const PackedInt32Array roots = p_surface["roots"];
	const PackedVector3Array bounds = p_surface["bounds"];
	const PackedVector3Array cluster_bounds = p_surface["cluster_bounds"];
	const PackedFloat32Array errors = p_surface["errors"];
	const int64_t group_count = groups.size() / 5;
	const int64_t cluster_count = clusters.size() / 3;
	const int vertex_count = p_surface["vertex_count"];
	const int triangle_limit = p_surface.get("cluster_triangles", 0);
	if (groups.is_empty() || groups.size() % 5 || clusters.is_empty() || clusters.size() % 3 ||
			bounds.size() != group_count * 2 || cluster_bounds.size() != cluster_count * 2 || errors.size() != group_count ||
			indices.is_empty() || indices.size() % 3 || roots.is_empty() || triangle_limit < 4 || triangle_limit > 124 ||
			group_count > (1 << 20) || cluster_count > (1 << 22)) {
		return false;
	}
	for (int index : indices) {
		if (index < 0 || index >= vertex_count) {
			return false;
		}
	}
	Vector<uint8_t> seen_clusters;
	Vector<uint8_t> seen_children;
	Vector<uint8_t> seen_roots;
	seen_clusters.resize_initialized(cluster_count);
	seen_children.resize_initialized(group_count);
	seen_roots.resize_initialized(group_count);
	int64_t next_cluster = 0;
	int64_t next_index = 0;
	int64_t next_child = 0;
	int64_t leaf_indices = 0;
	for (int64_t group = 0; group < group_count; group++) {
		const int64_t first = groups[group * 5];
		const int64_t count = groups[group * 5 + 1];
		const int64_t first_child = groups[group * 5 + 2];
		const int64_t child_count = groups[group * 5 + 3];
		const int parent = groups[group * 5 + 4];
		const AABB box(bounds[group * 2], bounds[group * 2 + 1]);
		if (!box.is_finite() || box.size.x < 0 || box.size.y < 0 || box.size.z < 0 || !Math::is_finite(errors[group]) || errors[group] < 0 ||
				first != next_cluster || count <= 0 || first + count > cluster_count || first_child != next_child || child_count < 0 || first_child + child_count > children.size() ||
				parent < -1 || parent >= group_count || (parent != -1 && parent <= group) || (child_count == 0 && errors[group] != 0)) {
			return false;
		}
		const AABB expanded = box.grow(0.0001 * (1.0 + box.size.length()));
		for (int64_t c = first; c < first + count; c++) {
			const int64_t first_index = clusters[c * 3];
			const int64_t index_count = clusters[c * 3 + 1];
			const AABB cluster_box(cluster_bounds[c * 2], cluster_bounds[c * 2 + 1]);
			if (seen_clusters[c] || clusters[c * 3 + 2] != group || first_index != next_index || index_count <= 0 || index_count % 3 ||
					index_count > triangle_limit * 3 || first_index + index_count > indices.size() || !cluster_box.is_finite() ||
					cluster_box.size.x < 0 || cluster_box.size.y < 0 || cluster_box.size.z < 0 || !expanded.encloses(cluster_box)) {
				return false;
			}
			seen_clusters.write[c] = 1;
			next_index += index_count;
			if (child_count == 0) {
				leaf_indices += index_count;
			}
		}
		for (int64_t c = first_child; c < first_child + child_count; c++) {
			const int child = children[c];
			if (child < 0 || child >= group || seen_children[child] || groups[child * 5 + 4] != group ||
					errors[child] > errors[group] || !expanded.encloses(AABB(bounds[child * 2], bounds[child * 2 + 1]))) {
				return false;
			}
			seen_children.write[child] = 1;
		}
		next_cluster += count;
		next_child += child_count;
	}
	if (next_cluster != cluster_count || next_index != indices.size() || next_child != children.size() || leaf_indices != int64_t(p_surface["source_index_count"])) {
		return false;
	}
	for (int root : roots) {
		if (root < 0 || root >= group_count || seen_roots[root] || groups[root * 5 + 4] != -1) {
			return false;
		}
		seen_roots.write[root] = 1;
	}
	for (int64_t i = 0; i < group_count; i++) {
		if (bool(seen_roots[i]) == bool(seen_children[i])) {
			return false;
		}
	}
	return true;
}

} // namespace MeshClusterDataFormat

#endif // MESH_CLUSTER_DATA_FORMAT_H
