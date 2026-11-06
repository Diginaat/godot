/**************************************************************************/
/*  mesh_cluster_data.cpp                                                 */
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

#include "mesh_cluster_data.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/object/class_db.h"
#include "servers/rendering/storage/mesh_cluster_data_format.h"

#include <cmath>

MeshClusterData::GenerateFunc MeshClusterData::generate_func = nullptr;

bool MeshClusterData::validate_surface(const Dictionary &p_surface) {
	return MeshClusterDataFormat::validate_surface(p_surface);
}

Error MeshClusterData::set_data_checked(const Array &p_data) {
	if (p_data.is_empty() || p_data.size() > 256) {
		return ERR_INVALID_DATA;
	}
	for (int surface = 0; surface < p_data.size(); surface++) {
		if (p_data[surface].get_type() != Variant::DICTIONARY || !validate_surface(p_data[surface])) {
			return ERR_INVALID_DATA;
		}
	}
	data = p_data.duplicate(true);
	emit_changed();
	return OK;
}

void MeshClusterData::_set_data(const Array &p_data) {
	ERR_FAIL_COND_MSG(set_data_checked(p_data) != OK, "Invalid cluster hierarchy data.");
}

Error MeshClusterData::generate(const Ref<Mesh> &p_mesh, int p_cluster_triangles, int p_group_size) {
	if (p_mesh.is_null() || p_cluster_triangles < 4 || p_cluster_triangles > 124 || p_cluster_triangles % 4 || p_group_size < 2 || p_group_size > 16) {
		return ERR_INVALID_PARAMETER;
	}
	if (!generate_func) {
		return ERR_UNAVAILABLE;
	}
	Array generated;
	const Error error = generate_func(p_mesh, p_cluster_triangles, p_group_size, generated);
	if (error != OK) {
		return error;
	}
	for (int surface = 0; surface < generated.size(); surface++) {
		Dictionary record = generated[surface];
		record["source_signature"] = _surface_signature(p_mesh->surface_get_arrays(surface));
	}
	return set_data_checked(generated);
}

PackedByteArray MeshClusterData::_surface_signature(const Array &p_arrays) {
	int length = 0;
	ERR_FAIL_COND_V(encode_variant(p_arrays, nullptr, length) != OK, PackedByteArray());
	PackedByteArray encoded;
	encoded.resize(length);
	ERR_FAIL_COND_V(encode_variant(p_arrays, encoded.ptrw(), length) != OK, PackedByteArray());
	PackedByteArray signature;
	signature.resize(32);
	ERR_FAIL_COND_V(CryptoCore::sha256(encoded.ptr(), encoded.size(), signature.ptrw()) != OK, PackedByteArray());
	return signature;
}

PackedByteArray MeshClusterData::_page_hash(const Dictionary &p_surface) {
	CryptoCore::SHA256Context context;
	ERR_FAIL_COND_V(context.start() != OK, PackedByteArray());
	for (const char *key : { "indices", "groups", "clusters" }) {
		const PackedInt32Array values = p_surface[key];
		ERR_FAIL_COND_V(context.update(reinterpret_cast<const uint8_t *>(values.ptr()), values.size() * sizeof(int32_t)) != OK, PackedByteArray());
	}
	PackedByteArray hash;
	hash.resize(32);
	ERR_FAIL_COND_V(context.finish(hash.ptrw()) != OK, PackedByteArray());
	return hash;
}

Error MeshClusterData::save_pages(const String &p_path, bool p_keep_indices) {
	ERR_FAIL_COND_V_MSG(data.is_empty(), ERR_UNCONFIGURED, "No cluster hierarchy to page.");
	ERR_FAIL_COND_V(p_path.is_empty(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(are_indices_paged(), ERR_UNAVAILABLE, "Cluster indices are already paged out; generate the hierarchy again to write another page file.");
	Vector<PackedByteArray> hashes;
	uint64_t offset = MeshClusterDataFormat::PAGE_HEADER_BYTES + uint64_t(data.size()) * MeshClusterDataFormat::PAGE_SURFACE_BYTES;
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	ERR_FAIL_COND_V_MSG(file.is_null(), error, vformat("Cannot write cluster LOD page file \"%s\".", p_path));
	file->store_32(MeshClusterDataFormat::PAGE_MAGIC);
	file->store_32(MeshClusterDataFormat::PAGE_VERSION);
	file->store_32(data.size());
	for (int surface = 0; surface < data.size(); surface++) {
		const Dictionary record = data[surface];
		const PackedByteArray hash = _page_hash(record);
		ERR_FAIL_COND_V(hash.size() != 32, ERR_BUG);
		hashes.push_back(hash);
		const PackedInt32Array indices = record["indices"];
		file->store_buffer(hash);
		file->store_32(indices.size());
		file->store_32(0);
		file->store_64(offset);
		offset += uint64_t(indices.size()) * sizeof(uint32_t);
	}
	for (int surface = 0; surface < data.size(); surface++) {
		// Validated indices are nonnegative; every supported platform is little-endian.
		const PackedInt32Array indices = Dictionary(data[surface])["indices"];
		file->store_buffer(reinterpret_cast<const uint8_t *>(indices.ptr()), uint64_t(indices.size()) * sizeof(int32_t));
	}
	file->flush();
	if (file->get_error() != OK) {
		ERR_FAIL_V_MSG(ERR_FILE_CANT_WRITE, vformat("Cannot write cluster LOD page file \"%s\".", p_path));
	}
	file.unref();
	// Record the file only once it is complete, so a renderer never pages from a partial file.
	for (int surface = 0; surface < data.size(); surface++) {
		Dictionary record = data[surface];
		record["page_file"] = p_path;
		record["page_hash"] = hashes[surface];
		record["page_surface"] = surface;
		if (!p_keep_indices) {
			// Keep only the roots: renderers need them to draw a complete cut
			// before any page is read.
			const PackedInt32Array indices = record["indices"];
			const PackedInt32Array groups = record["groups"];
			const PackedInt32Array clusters = record["clusters"];
			const PackedInt32Array roots = record["roots"];
			Vector<int> sorted_roots;
			for (int root : roots) {
				sorted_roots.push_back(root);
			}
			sorted_roots.sort();
			PackedInt32Array root_indices;
			for (int root : sorted_roots) {
				const int first = groups[root * 5];
				const int last = first + groups[root * 5 + 1] - 1;
				const int begin = clusters[first * 3];
				root_indices.append_array(indices.slice(begin, clusters[last * 3] + clusters[last * 3 + 1]));
			}
			record["index_count"] = indices.size();
			record["indices"] = root_indices;
		}
	}
	emit_changed();
	return OK;
}

bool MeshClusterData::are_indices_paged() const {
	return !data.is_empty() && Dictionary(data[0]).has("index_count");
}

void MeshClusterData::clear_pages() {
	ERR_FAIL_COND_MSG(are_indices_paged(), "Cluster indices are paged out; the page file is their only copy.");
	bool changed = false;
	for (int surface = 0; surface < data.size(); surface++) {
		Dictionary record = data[surface];
		changed = record.erase("page_file") || changed;
		record.erase("page_hash");
		record.erase("page_surface");
	}
	if (changed) {
		emit_changed();
	}
}

String MeshClusterData::get_page_file() const {
	return data.is_empty() ? String() : String(Dictionary(data[0]).get("page_file", String()));
}

bool MeshClusterData::matches_mesh(const Ref<Mesh> &p_mesh) const {
	if (p_mesh.is_null() || data.is_empty() || p_mesh->get_blend_shape_count() != 0 || p_mesh->get_surface_count() != data.size()) {
		return false;
	}
	for (int surface = 0; surface < data.size(); surface++) {
		const Dictionary record = data[surface];
		if (p_mesh->surface_get_primitive_type(surface) != Mesh::PRIMITIVE_TRIANGLES ||
				p_mesh->surface_get_array_len(surface) != int(record["vertex_count"]) ||
				record.get("source_signature", Variant()).get_type() != Variant::PACKED_BYTE_ARRAY) {
			return false;
		}
		const PackedByteArray signature = record["source_signature"];
		if (signature.size() != 32 || signature != _surface_signature(p_mesh->surface_get_arrays(surface))) {
			return false;
		}
	}
	return true;
}

Dictionary MeshClusterData::get_surface_data(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, data.size(), Dictionary());
	Dictionary surface = data[p_surface];
	return surface.duplicate(true);
}

Dictionary MeshClusterData::get_residency_state(int p_surface, const PackedByteArray &p_resident_groups) const {
	ERR_FAIL_INDEX_V(p_surface, data.size(), Dictionary());
	const Dictionary surface = data[p_surface];
	const PackedInt32Array groups = surface["groups"];
	const PackedInt32Array children = surface["children"];
	const PackedInt32Array roots = surface["roots"];
	ERR_FAIL_COND_V(p_resident_groups.size() != groups.size() / 5, Dictionary());
	PackedByteArray flags;
	flags.resize(p_resident_groups.size());
	// Validated children precede parents. A missing representation can still
	// be covered entirely by resident descendants; ancestor pages need not pin.
	for (int group = 0; group < flags.size(); group++) {
		ERR_FAIL_COND_V(p_resident_groups[group] > 1, Dictionary());
		bool covered = groups[group * 5 + 3] != 0;
		for (int c = 0; c < groups[group * 5 + 3]; c++) {
			covered &= (flags[children[groups[group * 5 + 2] + c]] & 2) != 0;
		}
		flags.write[group] = p_resident_groups[group] ? 3 : (covered ? 2 : 0);
	}
	bool complete = true;
	for (int root : roots) {
		complete &= (flags[root] & 2) != 0;
	}
	Dictionary result;
	result["groups"] = flags;
	result["complete"] = complete;
	return result;
}

PackedInt32Array MeshClusterData::select_clusters(int p_surface, const Vector3 &p_camera_position, float p_projection_scale, float p_error_pixels) const {
	ERR_FAIL_INDEX_V(p_surface, data.size(), PackedInt32Array());
	ERR_FAIL_COND_V(!p_camera_position.is_finite() || !Math::is_finite(p_projection_scale) || p_projection_scale <= 0 || !Math::is_finite(p_error_pixels) || p_error_pixels < 0, PackedInt32Array());
	const Dictionary surface = data[p_surface];
	const PackedInt32Array groups = surface["groups"];
	const PackedVector3Array bounds = surface["bounds"];
	const PackedFloat32Array errors = surface["errors"];
	const PackedInt32Array children = surface["children"];
	const PackedInt32Array roots = surface["roots"];
	Vector<int> pending;
	for (int root : roots) {
		pending.push_back(root);
	}
	PackedInt32Array selected;
	while (!pending.is_empty()) {
		const int i = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		double delta[3];
		for (int axis = 0; axis < 3; axis++) {
			const double half_extent = double(bounds[i * 2 + 1][axis]) * 0.5;
			const double center = double(bounds[i * 2][axis]) + half_extent;
			delta[axis] = MAX(std::abs(double(p_camera_position[axis]) - center) - half_extent, 0.0);
		}
		const double distance = std::hypot(delta[0], delta[1], delta[2]);
		const double projected = double(errors[i]) * double(p_projection_scale) / MAX(distance, 0.0001);
		// Root traversal guarantees a complete non-overlapping cut even at
		// rounded AABB boundaries; independent parent/self tests need stronger
		// numerical monotonicity than serialized floats can promise.
		if (groups[i * 5 + 3] == 0 || projected <= double(p_error_pixels)) {
			for (int c = 0; c < groups[i * 5 + 1]; c++) {
				selected.push_back(groups[i * 5] + c);
			}
		} else {
			for (int c = groups[i * 5 + 3] - 1; c >= 0; c--) {
				pending.push_back(children[groups[i * 5 + 2] + c]);
			}
		}
	}
	return selected;
}

void MeshClusterData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "mesh", "cluster_triangles", "group_size"), &MeshClusterData::generate, DEFVAL(64), DEFVAL(8));
	ClassDB::bind_method(D_METHOD("matches_mesh", "mesh"), &MeshClusterData::matches_mesh);
	ClassDB::bind_method(D_METHOD("set_data_checked", "data"), &MeshClusterData::set_data_checked);
	ClassDB::bind_method(D_METHOD("get_data"), &MeshClusterData::get_data);
	ClassDB::bind_method(D_METHOD("_set_data", "data"), &MeshClusterData::_set_data);
	ClassDB::bind_method(D_METHOD("get_surface_count"), &MeshClusterData::get_surface_count);
	ClassDB::bind_method(D_METHOD("get_surface_data", "surface"), &MeshClusterData::get_surface_data);
	ClassDB::bind_method(D_METHOD("get_residency_state", "surface", "resident_groups"), &MeshClusterData::get_residency_state);
	ClassDB::bind_method(D_METHOD("select_clusters", "surface", "camera_position", "projection_scale", "error_pixels"), &MeshClusterData::select_clusters);
	ClassDB::bind_method(D_METHOD("save_pages", "path", "keep_indices"), &MeshClusterData::save_pages, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("are_indices_paged"), &MeshClusterData::are_indices_paged);
	ClassDB::bind_method(D_METHOD("clear_pages"), &MeshClusterData::clear_pages);
	ClassDB::bind_method(D_METHOD("get_page_file"), &MeshClusterData::get_page_file);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "_set_data", "get_data");
}
