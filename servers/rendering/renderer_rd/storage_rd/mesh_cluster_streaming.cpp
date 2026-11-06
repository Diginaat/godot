/**************************************************************************/
/*  mesh_cluster_streaming.cpp                                            */
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

#include "mesh_storage.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/storage/mesh_cluster_data_format.h"

using namespace RendererRD;

// Index uploads per frame (4 MiB); the remaining requests return next feedback.
static constexpr uint32_t CLUSTER_STREAM_UPLOAD_INDICES = 1u << 20;
// Disk reads in flight (4 MiB); further disk requests return next feedback.
static constexpr uint32_t CLUSTER_STREAM_READ_INDICES = 1u << 20;
// A group drawn in the latest feedback is never evicted for another request.
static constexpr uint64_t CLUSTER_STREAM_KEEP_FRAMES = 2;

static uint64_t _cluster_streaming_budget_bytes() {
	const double megabytes = GLOBAL_GET("rendering/geometry/cluster_lod/streaming/budget_mb");
	return uint64_t(CLAMP(megabytes, 0.0, 4096.0) * 1024.0 * 1024.0);
}

bool MeshStorage::_cluster_pool_create(uint64_t p_budget_bytes) {
	RD *rd = RD::get_singleton();
	if (cluster_pool.index_buffer.is_valid()) {
		// Freeing the buffer also frees its dependent index array. Earlier frames
		// that still read it keep it alive through RD's deferred destruction.
		rd->free_rid(cluster_pool.index_buffer);
	}
	cluster_pool.index_buffer = RID();
	cluster_pool.index_array = RID();
	cluster_pool.capacity = 0;
	cluster_pool.free_ranges.clear();
	cluster_pool.resident_indices = 0;
	cluster_pool.budget_bytes = p_budget_bytes;
	const uint32_t capacity = uint32_t(MIN(p_budget_bytes / sizeof(uint32_t), uint64_t(1) << 28));
	if (capacity < 3) {
		return false;
	}
	cluster_pool.index_buffer = rd->index_buffer_create(capacity, RD::INDEX_BUFFER_FORMAT_UINT32);
	if (cluster_pool.index_buffer.is_null()) {
		return false;
	}
	cluster_pool.index_array = rd->index_array_create(cluster_pool.index_buffer, 0, capacity);
	if (cluster_pool.index_array.is_null()) {
		rd->free_rid(cluster_pool.index_buffer);
		cluster_pool.index_buffer = RID();
		return false;
	}
	cluster_pool.capacity = capacity;
	cluster_pool.free_ranges.push_back({ 0, capacity });
	return true;
}

bool MeshStorage::_cluster_pool_allocate(uint32_t p_size, uint32_t &r_offset) {
	// First fit; the free list stays short because neighbours coalesce.
	for (uint32_t i = 0; i < cluster_pool.free_ranges.size(); i++) {
		ClusterStreamingPool::Range &range = cluster_pool.free_ranges[i];
		if (range.size >= p_size) {
			r_offset = range.offset;
			range.offset += p_size;
			range.size -= p_size;
			if (range.size == 0) {
				cluster_pool.free_ranges.remove_at(i);
			}
			cluster_pool.resident_indices += p_size;
			return true;
		}
	}
	return false;
}

void MeshStorage::_cluster_pool_free(uint32_t p_offset, uint32_t p_size) {
	LocalVector<ClusterStreamingPool::Range> &ranges = cluster_pool.free_ranges;
	uint32_t i = 0;
	while (i < ranges.size() && ranges[i].offset < p_offset) {
		i++;
	}
	ranges.insert(i, { p_offset, p_size });
	if (i + 1 < ranges.size() && ranges[i].offset + ranges[i].size == ranges[i + 1].offset) {
		ranges[i].size += ranges[i + 1].size;
		ranges.remove_at(i + 1);
	}
	if (i > 0 && ranges[i - 1].offset + ranges[i - 1].size == ranges[i].offset) {
		ranges[i - 1].size += ranges[i].size;
		ranges.remove_at(i);
	}
	cluster_pool.resident_indices -= p_size;
}

bool MeshStorage::_cluster_pool_evict(uint32_t p_size, uint64_t p_before_frame) {
	struct Candidate {
		uint64_t last_used;
		Mesh::Surface *surface;
		uint32_t group;
		bool operator<(const Candidate &p_other) const { return last_used < p_other.last_used; }
	};
	LocalVector<Candidate> candidates;
	for (Mesh::Surface *surface : cluster_pool.surfaces) {
		const ClusterStreaming *streaming = surface->cluster_data.streaming;
		for (uint32_t group = 0; group < streaming->slot.size(); group++) {
			if (streaming->slot[group] >= 0 && !streaming->pinned[group] && streaming->last_used[group] < p_before_frame) {
				candidates.push_back({ streaming->last_used[group], surface, group });
			}
		}
	}
	// Least recently drawn first. Evicting any non-root group keeps every cut
	// complete: the residency table then marks it uncovered and its resident
	// ancestors (at least the pinned root) draw instead.
	candidates.sort();
	for (const Candidate &candidate : candidates) {
		_cluster_streaming_unload(candidate.surface, candidate.group);
		for (const ClusterStreamingPool::Range &range : cluster_pool.free_ranges) {
			if (range.size >= p_size) {
				return true;
			}
		}
	}
	return false;
}

bool MeshStorage::_cluster_streaming_store(Mesh::Surface *p_surface, uint32_t p_group, const uint32_t *p_indices) {
	ClusterStreaming *streaming = p_surface->cluster_data.streaming;
	if (streaming->slot[p_group] >= 0) {
		return true;
	}
	const uint32_t size = streaming->index_count[p_group];
	uint32_t offset = 0;
	if (!_cluster_pool_allocate(size, offset)) {
		return false;
	}
	RD::get_singleton()->buffer_update(cluster_pool.index_buffer, offset * sizeof(uint32_t), size * sizeof(uint32_t), p_indices);
	streaming->slot[p_group] = offset;
	// A new group counts as used now, so the next request cannot evict it at once.
	streaming->last_used[p_group] = cluster_pool.frame;
	streaming->dirty = true;
	cluster_pool.loads++;
	return true;
}

bool MeshStorage::_cluster_streaming_load(Mesh::Surface *p_surface, uint32_t p_group) {
	ClusterStreaming *streaming = p_surface->cluster_data.streaming;
	if (streaming->slot[p_group] >= 0) {
		return true;
	}
	const uint32_t offset = streaming->memory_offset[p_group];
	if (offset == UINT32_MAX) {
		return false;
	}
	return _cluster_streaming_store(p_surface, p_group, streaming->indices.ptr() + offset);
}

uint64_t MeshStorage::_cluster_streaming_keep_after() const {
	return cluster_pool.feedback_frame > CLUSTER_STREAM_KEEP_FRAMES ? cluster_pool.feedback_frame - CLUSTER_STREAM_KEEP_FRAMES : 0;
}

bool MeshStorage::_cluster_page_file_check(const String &p_path, int p_surface, const PackedByteArray &p_hash, uint32_t p_index_count, uint64_t &r_offset) {
	using namespace MeshClusterDataFormat;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || p_hash.size() != 32) {
		return false;
	}
	const uint64_t length = file->get_length();
	if (length < PAGE_HEADER_BYTES || file->get_32() != PAGE_MAGIC || file->get_32() != PAGE_VERSION) {
		return false;
	}
	const uint32_t surfaces = file->get_32();
	const uint64_t header_end = PAGE_HEADER_BYTES + uint64_t(surfaces) * PAGE_SURFACE_BYTES;
	if (p_surface < 0 || uint32_t(p_surface) >= surfaces || length < header_end) {
		return false;
	}
	file->seek(PAGE_HEADER_BYTES + uint64_t(p_surface) * PAGE_SURFACE_BYTES);
	uint8_t hash[32];
	if (file->get_buffer(hash, 32) != 32 || memcmp(hash, p_hash.ptr(), 32) != 0) {
		return false;
	}
	const uint32_t count = file->get_32();
	file->get_32();
	const uint64_t offset = file->get_64();
	if (file->get_error() != OK || count != p_index_count || offset < header_end || offset > length || (length - offset) / sizeof(uint32_t) < count) {
		return false;
	}
	r_offset = offset;
	return true;
}

bool MeshStorage::_cluster_page_file_read_all(const Dictionary &p_record, PackedInt32Array &r_indices) {
	// Render thread, once per setup: paged-out records drawn without streaming.
	const String path = p_record["page_file"];
	const uint32_t count = int(p_record["index_count"]);
	uint64_t offset = 0;
	Ref<FileAccess> file;
	if (_cluster_page_file_check(path, p_record["page_surface"], p_record["page_hash"], count, offset)) {
		file = FileAccess::open(path, FileAccess::READ);
	}
	bool valid = file.is_valid();
	if (valid) {
		file->seek(offset);
		r_indices.resize(count);
		const uint64_t bytes = uint64_t(count) * sizeof(uint32_t);
		valid = file->get_buffer(reinterpret_cast<uint8_t *>(r_indices.ptrw()), bytes) == bytes;
		const uint32_t vertex_count = int(p_record["vertex_count"]);
		for (uint32_t i = 0; valid && i < count; i++) {
			valid = uint32_t(r_indices[i]) < vertex_count;
		}
	}
	if (!valid) {
		WARN_PRINT(vformat("Cluster LOD page file \"%s\" is missing, damaged or does not match its mesh, and the mesh holds only its root indices; it draws without cluster LOD.", path));
	}
	return valid;
}

void MeshStorage::_cluster_page_batch_read(void *p_batch) {
	// Worker thread: touches only its own batch until the render thread waits for it.
	ClusterStreamingPool::PageBatch *batch = static_cast<ClusterStreamingPool::PageBatch *>(p_batch);
	Ref<FileAccess> file = FileAccess::open(batch->path, FileAccess::READ);
	uint32_t *data = batch->data.ptrw();
	for (ClusterStreamingPool::PageRead &read : batch->reads) {
		read.ok = false;
		if (file.is_null()) {
			continue;
		}
		file->seek(read.file_offset);
		uint32_t *indices = data + read.data_offset;
		const uint64_t bytes = uint64_t(read.count) * sizeof(uint32_t);
		if (file->get_buffer(reinterpret_cast<uint8_t *>(indices), bytes) != bytes) {
			continue;
		}
		// A changed or damaged file must never index outside the vertex buffer.
		bool valid = true;
		for (uint32_t i = 0; i < read.count; i++) {
			valid = valid && indices[i] < read.vertex_count;
		}
		read.ok = valid;
	}
}

void MeshStorage::_cluster_page_batches_finish(bool p_discard) {
	WorkerThreadPool *workers = WorkerThreadPool::get_singleton();
	for (uint32_t i = 0; i < cluster_pool.page_batches.size();) {
		ClusterStreamingPool::PageBatch *batch = cluster_pool.page_batches[i];
		if (!p_discard && !workers->is_task_completed(batch->task)) {
			i++;
			continue;
		}
		workers->wait_for_task_completion(batch->task);
		const uint64_t keep_after = _cluster_streaming_keep_after();
		for (const ClusterStreamingPool::PageRead &read : batch->reads) {
			cluster_pool.reading_indices -= read.count;
			Mesh::Surface **surface = p_discard ? nullptr : cluster_pool.surface_ids.getptr(read.surface_id);
			if (!surface) {
				// Released, or given new cluster data, while the read was in flight.
				continue;
			}
			ClusterStreaming *streaming = (*surface)->cluster_data.streaming;
			if (!read.ok) {
				// Keep it unread (its ancestors draw) instead of failing every frame.
				streaming->reading[read.group] = 2;
				cluster_pool.disk_errors++;
				continue;
			}
			streaming->reading[read.group] = 0;
			cluster_pool.disk_reads++;
			cluster_pool.disk_bytes += uint64_t(read.count) * sizeof(uint32_t);
			const uint32_t *indices = batch->data.ptr() + read.data_offset;
			if (!_cluster_streaming_store(*surface, read.group, indices) &&
					!(_cluster_pool_evict(read.count, keep_after) && _cluster_streaming_store(*surface, read.group, indices))) {
				// The pool is full of recently drawn groups; a later feedback asks again.
				cluster_pool.deferred++;
			}
		}
		memdelete(batch);
		cluster_pool.page_batches.remove_at(i);
	}
}

void MeshStorage::_cluster_streaming_unload(Mesh::Surface *p_surface, uint32_t p_group) {
	ClusterStreaming *streaming = p_surface->cluster_data.streaming;
	if (streaming->slot[p_group] < 0) {
		return;
	}
	_cluster_pool_free(uint32_t(streaming->slot[p_group]), streaming->index_count[p_group]);
	streaming->slot[p_group] = -1;
	streaming->dirty = true;
	cluster_pool.evictions++;
}

void MeshStorage::_cluster_streaming_upload_table(Mesh::Surface *p_surface) {
	ClusterStreaming *streaming = p_surface->cluster_data.streaming;
	const uint32_t group_count = streaming->slot.size();
	LocalVector<uint32_t> table;
	table.resize((group_count + 1) * 4);
	// Validated children precede parents, so one forward pass computes
	// "covered by itself or by resident descendants" (bit 1).
	LocalVector<uint8_t> flags;
	flags.resize(group_count);
	bool complete = true;
	for (uint32_t group = 0; group < group_count; group++) {
		bool covered = streaming->child_count[group] != 0;
		for (uint32_t c = 0; c < streaming->child_count[group] && covered; c++) {
			covered = (flags[streaming->children[streaming->first_child[group] + c]] & 2) != 0;
		}
		const bool resident = streaming->slot[group] >= 0;
		flags[group] = resident ? 3 : (covered ? 2 : 0);
		uint32_t *entry = &table[(group + 1) * 4];
		entry[0] = flags[group];
		entry[1] = streaming->first_child[group];
		entry[2] = resident ? uint32_t(streaming->slot[group]) : 0;
		entry[3] = 0;
		if (streaming->pinned[group]) {
			complete = complete && (flags[group] & 2);
		}
	}
	table[0] = complete;
	table[1] = table[2] = table[3] = 0;
	RD::get_singleton()->buffer_update(streaming->residency, 0, table.size() * sizeof(uint32_t), table.ptr());
	streaming->complete = complete;
	streaming->dirty = false;
}

bool MeshStorage::_cluster_streaming_setup(Mesh::Surface *p_surface, const Dictionary &p_record) {
	RD *rd = RD::get_singleton();
	if (cluster_pool.index_buffer.is_null() && !_cluster_pool_create(_cluster_streaming_budget_bytes())) {
		return false;
	}
	const PackedInt32Array groups = p_record["groups"];
	const PackedInt32Array clusters = p_record["clusters"];
	const PackedInt32Array indices = p_record["indices"];
	const PackedInt32Array children = p_record["children"];
	const PackedInt32Array roots = p_record["roots"];
	const uint32_t group_count = groups.size() / 5;
	// Paged-out records hold only the roots' indices, in group order.
	const bool paged = p_record.has("index_count");
	const uint32_t total_indices = paged ? uint32_t(int(p_record["index_count"])) : uint32_t(indices.size());
	ClusterStreaming *streaming = memnew(ClusterStreaming);
	streaming->id = cluster_pool.next_surface_id++;
	streaming->total_indices = total_indices;
	streaming->vertex_count = int(p_record["vertex_count"]);
	streaming->first_index.resize(group_count);
	streaming->index_count.resize(group_count);
	streaming->first_child.resize(group_count);
	streaming->child_count.resize(group_count);
	for (uint32_t group = 0; group < group_count; group++) {
		const int first = groups[group * 5];
		const int last = first + groups[group * 5 + 1] - 1;
		streaming->first_index[group] = clusters[first * 3];
		streaming->index_count[group] = clusters[last * 3] + clusters[last * 3 + 1] - clusters[first * 3];
		streaming->first_child[group] = groups[group * 5 + 2];
		streaming->child_count[group] = groups[group * 5 + 3];
	}
	streaming->children.resize(children.size());
	memcpy(streaming->children.ptrw(), children.ptr(), children.size() * sizeof(uint32_t));
	streaming->slot.resize(group_count);
	streaming->last_used.resize(group_count);
	streaming->pinned.resize(group_count);
	for (uint32_t group = 0; group < group_count; group++) {
		streaming->slot[group] = -1;
		streaming->last_used[group] = 0;
		streaming->pinned[group] = 0;
	}
	for (int root : roots) {
		streaming->pinned[root] = 1;
	}
	if (p_record.has("page_file")) {
		const String path = p_record["page_file"];
		if (_cluster_page_file_check(path, p_record["page_surface"], p_record["page_hash"], total_indices, streaming->page_offset)) {
			streaming->page_file = path;
		} else if (paged) {
			// The resident fallback reads the file again and reports it.
			memdelete(streaming);
			return false;
		} else {
			WARN_PRINT(vformat("Cluster LOD page file \"%s\" is missing or does not match its mesh; streaming the mesh from memory.", path));
		}
	}
	// From memory every group's indices stay held; from a page file only the roots'.
	streaming->memory_offset.resize(group_count);
	streaming->reading.resize(group_count);
	uint32_t held = 0;
	for (uint32_t group = 0; group < group_count; group++) {
		const bool in_memory = streaming->page_file.is_empty() || streaming->pinned[group];
		streaming->memory_offset[group] = in_memory ? held : UINT32_MAX;
		held += in_memory ? streaming->index_count[group] : 0;
		streaming->reading[group] = 0;
	}
	streaming->indices.resize(held);
	uint32_t *held_indices = streaming->indices.ptrw();
	uint32_t paged_offset = 0;
	for (uint32_t group = 0; group < group_count; group++) {
		if (streaming->memory_offset[group] != UINT32_MAX) {
			const int32_t *source = indices.ptr() + (paged ? paged_offset : streaming->first_index[group]);
			memcpy(held_indices + streaming->memory_offset[group], source, streaming->index_count[group] * sizeof(uint32_t));
			paged_offset += streaming->index_count[group];
		}
	}
	streaming->residency = rd->storage_buffer_create((group_count + 1) * 4 * sizeof(uint32_t));
	Vector<uint8_t> child_bytes;
	child_bytes.resize(MAX(children.size(), 1) * sizeof(uint32_t));
	memcpy(child_bytes.ptrw(), streaming->children.ptr(), children.size() * sizeof(uint32_t));
	streaming->children_buffer = rd->storage_buffer_create(child_bytes.size(), child_bytes);
	p_surface->cluster_data.streaming = streaming;
	if (streaming->residency.is_null() || streaming->children_buffer.is_null()) {
		_cluster_streaming_release(p_surface);
		return false;
	}
	cluster_pool.surfaces.push_back(p_surface);
	cluster_pool.surface_ids.insert(streaming->id, p_surface);
	cluster_pool.layout_dirty = true;
	// Roots first: without all of them the surface keeps conventional rendering.
	for (int root : roots) {
		if (!_cluster_streaming_load(p_surface, root) && !(_cluster_pool_evict(streaming->index_count[root], UINT64_MAX) && _cluster_streaming_load(p_surface, root))) {
			_cluster_streaming_release(p_surface);
			return false;
		}
	}
	p_surface->cluster_data.index_array = cluster_pool.index_array;
	_cluster_streaming_upload_table(p_surface);
	return true;
}

void MeshStorage::_cluster_streaming_release(Mesh::Surface *p_surface) {
	ClusterStreaming *streaming = p_surface->cluster_data.streaming;
	if (!streaming) {
		return;
	}
	for (uint32_t group = 0; group < streaming->slot.size(); group++) {
		if (streaming->slot[group] >= 0) {
			_cluster_pool_free(uint32_t(streaming->slot[group]), streaming->index_count[group]);
		}
	}
	for (RID resource : { streaming->residency, streaming->children_buffer }) {
		if (resource.is_valid()) {
			RD::get_singleton()->free_rid(resource);
		}
	}
	cluster_pool.surface_ids.erase(streaming->id);
	if (cluster_pool.surfaces.has(p_surface)) {
		cluster_pool.surfaces.erase(p_surface);
		cluster_pool.layout_dirty = true;
	}
	memdelete(streaming);
	p_surface->cluster_data.streaming = nullptr;
	// The pool owns the index array.
	p_surface->cluster_data.index_array = RID();
}

void MeshStorage::_cluster_streaming_feedback(const Vector<uint8_t> &p_data, uint64_t p_version, uint64_t p_frame) {
	MeshStorage *storage = MeshStorage::get_singleton();
	if (!storage) {
		return;
	}
	ClusterStreamingPool &pool = storage->cluster_pool;
	pool.readback_pending = false;
	// Results for an older request layout cannot be mapped back to groups.
	if (p_version != pool.layout_version || p_data.size() < int(pool.request_words * sizeof(uint32_t))) {
		return;
	}
	pool.feedback.resize(pool.request_words);
	memcpy(pool.feedback.ptrw(), p_data.ptr(), pool.request_words * sizeof(uint32_t));
	pool.feedback_version = p_version;
	pool.feedback_frame = p_frame;
	pool.feedback_ready = true;
}

void MeshStorage::_cluster_streaming_apply_feedback() {
	if (!cluster_pool.feedback_ready) {
		return;
	}
	cluster_pool.feedback_ready = false;
	if (cluster_pool.feedback_version != cluster_pool.layout_version || cluster_pool.index_buffer.is_null()) {
		return;
	}
	const uint32_t *words = cluster_pool.feedback.ptr();
	struct Request {
		Mesh::Surface *surface;
		uint32_t group;
	};
	LocalVector<Request> wanted;
	// Mark drawn groups first, so eviction never removes what the latest
	// feedback drew. Coarser groups (higher index) load first.
	for (Mesh::Surface *surface : cluster_pool.surfaces) {
		ClusterStreaming *streaming = surface->cluster_data.streaming;
		const uint32_t *surface_words = words + streaming->request_offset + 1;
		for (uint32_t group = streaming->slot.size(); group-- > 0;) {
			const uint32_t word = surface_words[group];
			if (word & 2u) {
				streaming->last_used[group] = cluster_pool.feedback_frame;
			}
			if ((word & 1u) && streaming->slot[group] < 0) {
				wanted.push_back({ surface, group });
			}
		}
	}
	const uint64_t keep_after = _cluster_streaming_keep_after();
	uint32_t upload_budget = CLUSTER_STREAM_UPLOAD_INDICES;
	bool pool_full = false;
	LocalVector<ClusterStreamingPool::PageBatch *> batches; // Started this frame, one per page file.
	for (const Request &request : wanted) {
		ClusterStreaming *streaming = request.surface->cluster_data.streaming;
		const uint32_t size = streaming->index_count[request.group];
		if (streaming->memory_offset[request.group] == UINT32_MAX) {
			// On disk only: read on a worker, upload once the read completes.
			// Groups whose read failed are not read again until the next setup.
			if (streaming->reading[request.group]) {
				continue;
			}
			if (cluster_pool.reading_indices + size > CLUSTER_STREAM_READ_INDICES) {
				cluster_pool.deferred++;
				continue;
			}
			ClusterStreamingPool::PageBatch *batch = nullptr;
			for (ClusterStreamingPool::PageBatch *candidate : batches) {
				if (candidate->path == streaming->page_file) {
					batch = candidate;
					break;
				}
			}
			if (!batch) {
				batch = memnew(ClusterStreamingPool::PageBatch);
				batch->path = streaming->page_file;
				batches.push_back(batch);
			}
			ClusterStreamingPool::PageRead read;
			read.surface_id = streaming->id;
			read.group = request.group;
			read.file_offset = streaming->page_offset + uint64_t(streaming->first_index[request.group]) * sizeof(uint32_t);
			read.count = size;
			read.vertex_count = streaming->vertex_count;
			read.data_offset = batch->reads.is_empty() ? 0 : batch->reads[batch->reads.size() - 1].data_offset + batch->reads[batch->reads.size() - 1].count;
			batch->reads.push_back(read);
			streaming->reading[request.group] = 1;
			cluster_pool.reading_indices += size;
			continue;
		}
		if (pool_full || size > upload_budget) {
			cluster_pool.deferred++;
			continue;
		}
		if (!_cluster_streaming_load(request.surface, request.group) &&
				!(_cluster_pool_evict(size, keep_after) && _cluster_streaming_load(request.surface, request.group))) {
			// The pool is full of recently drawn groups: keep the coarser cut.
			pool_full = true;
			cluster_pool.deferred++;
			continue;
		}
		upload_budget -= size;
	}
	for (ClusterStreamingPool::PageBatch *batch : batches) {
		const ClusterStreamingPool::PageRead &last = batch->reads[batch->reads.size() - 1];
		batch->data.resize(last.data_offset + last.count);
		batch->task = WorkerThreadPool::get_singleton()->add_native_task(&MeshStorage::_cluster_page_batch_read, batch, false, "Cluster LOD pages");
		cluster_pool.page_batches.push_back(batch);
	}
}

void MeshStorage::cluster_streaming_begin_frame() {
	const uint64_t frame = Engine::get_singleton()->get_frames_drawn();
	if (cluster_pool.frame == frame) {
		return;
	}
	cluster_pool.frame = frame;
	if (cluster_pool.surfaces.is_empty()) {
		// Reads of surfaces released while in flight still need collecting.
		_cluster_page_batches_finish(false);
		return;
	}
	RD *rd = RD::get_singleton();
	const uint64_t budget = _cluster_streaming_budget_bytes();
	if (budget != cluster_pool.budget_bytes) {
		// New budget: rebuild the pool and reload every root. Surfaces whose
		// roots no longer fit stay incomplete and keep conventional rendering.
		const bool created = _cluster_pool_create(budget);
		for (Mesh::Surface *surface : cluster_pool.surfaces) {
			ClusterStreaming *streaming = surface->cluster_data.streaming;
			for (uint32_t group = 0; group < streaming->slot.size(); group++) {
				streaming->slot[group] = -1;
			}
			for (uint32_t group = 0; created && group < streaming->slot.size(); group++) {
				if (streaming->pinned[group]) {
					_cluster_streaming_load(surface, group);
				}
			}
			surface->cluster_data.index_array = cluster_pool.index_array;
			streaming->dirty = true;
		}
		cluster_pool.feedback_ready = false;
	}
	if (cluster_pool.layout_dirty) {
		uint32_t words = 0;
		for (Mesh::Surface *surface : cluster_pool.surfaces) {
			surface->cluster_data.streaming->request_offset = words;
			words += surface->cluster_data.streaming->slot.size() + 1;
		}
		cluster_pool.request_words = words;
		if (cluster_pool.request_capacity < words) {
			if (cluster_pool.requests.is_valid()) {
				rd->free_rid(cluster_pool.requests);
			}
			cluster_pool.request_capacity = Math::next_power_of_2(words);
			cluster_pool.requests = rd->storage_buffer_create(cluster_pool.request_capacity * sizeof(uint32_t));
		}
		cluster_pool.layout_dirty = false;
		cluster_pool.layout_version++;
		cluster_pool.feedback_ready = false;
	}
	if (cluster_pool.requests.is_null()) {
		return;
	}
	_cluster_page_batches_finish(false);
	_cluster_streaming_apply_feedback();
	for (Mesh::Surface *surface : cluster_pool.surfaces) {
		if (surface->cluster_data.streaming->dirty) {
			_cluster_streaming_upload_table(surface);
		}
	}
	// Copy the requests of the previous frames' selections, then clear for this
	// frame. Both are recorded before this frame's selection dispatches.
	if (!cluster_pool.readback_pending) {
		cluster_pool.readback_pending = true;
		const Callable callback = callable_mp_static(&MeshStorage::_cluster_streaming_feedback).bind(cluster_pool.layout_version, frame);
		if (rd->buffer_get_data_async(cluster_pool.requests, callback, 0, cluster_pool.request_words * sizeof(uint32_t)) != OK) {
			cluster_pool.readback_pending = false;
		}
	}
	rd->buffer_clear(cluster_pool.requests, 0, cluster_pool.request_capacity * sizeof(uint32_t));
}

Dictionary MeshStorage::cluster_streaming_get_statistics() const {
	Dictionary result;
	uint32_t complete = 0;
	uint32_t disk_surfaces = 0;
	uint64_t source_indices = 0;
	uint64_t held_indices = 0;
	for (Mesh::Surface *surface : cluster_pool.surfaces) {
		const ClusterStreaming *streaming = surface->cluster_data.streaming;
		complete += streaming->complete;
		disk_surfaces += !streaming->page_file.is_empty();
		source_indices += streaming->total_indices;
		held_indices += streaming->indices.size();
	}
	result["clod_stream_surfaces"] = cluster_pool.surfaces.size();
	result["clod_stream_complete_surfaces"] = complete;
	result["clod_stream_pool_bytes"] = uint64_t(cluster_pool.capacity) * sizeof(uint32_t);
	result["clod_stream_resident_bytes"] = cluster_pool.resident_indices * sizeof(uint32_t);
	result["clod_stream_cooked_bytes"] = source_indices * sizeof(uint32_t);
	result["clod_stream_loads"] = cluster_pool.loads;
	result["clod_stream_evictions"] = cluster_pool.evictions;
	result["clod_stream_deferred"] = cluster_pool.deferred;
	result["clod_stream_memory_bytes"] = held_indices * sizeof(uint32_t);
	result["clod_stream_disk_surfaces"] = disk_surfaces;
	result["clod_stream_disk_reads"] = cluster_pool.disk_reads;
	result["clod_stream_disk_bytes"] = cluster_pool.disk_bytes;
	result["clod_stream_disk_errors"] = cluster_pool.disk_errors;
	result["clod_stream_disk_reading_bytes"] = uint64_t(cluster_pool.reading_indices) * sizeof(uint32_t);
	return result;
}
