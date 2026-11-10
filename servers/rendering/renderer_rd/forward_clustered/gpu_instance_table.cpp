/**************************************************************************/
/*  gpu_instance_table.cpp                                                */
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

#include "gpu_instance_table.h"

#include "core/math/math_funcs_binary.h"
#include "core/object/callable_mp.h"
#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

GPUInstanceTable *GPUInstanceTable::validating_table = nullptr;

void GPUInstanceTable::ensure_slot(uint32_t p_slot) {
	if (p_slot < records.size()) {
		return;
	}
	const uint32_t old_size = records.size();
	records.resize(p_slot + 1);
	dirty_flags.resize(p_slot + 1);
	memset(records.ptr() + old_size, 0, sizeof(Record) * (records.size() - old_size));
	memset(dirty_flags.ptr() + old_size, 0, dirty_flags.size() - old_size);
}

void GPUInstanceTable::mark_dirty(uint32_t p_slot) {
	ensure_slot(p_slot);
	if (!dirty_flags[p_slot]) {
		dirty_flags[p_slot] = 1;
		dirty_slots.push_back(p_slot);
	}
}

void GPUInstanceTable::set_record(uint32_t p_slot, const Record &p_record) {
	ensure_slot(p_slot);
	records[p_slot] = p_record;
}

GPUInstanceTable::UploadStats GPUInstanceTable::upload(bool p_skip_one) {
	UploadStats stats;
	RD *rd = RD::get_singleton();

	if (buffer.is_null() || buffer_capacity < records.size()) {
		// Grow to a power of two and start from the whole mirror: one upload
		// replaces every pending range. RD keeps the old buffer alive until
		// the frames that read it have finished.
		free_buffer();
		buffer_capacity = MAX(MIN_CAPACITY, Math::next_power_of_2(MAX(records.size(), 1u)));
		Vector<uint8_t> initial;
		initial.resize(buffer_capacity * sizeof(Record));
		memset(initial.ptrw(), 0, initial.size());
		memcpy(initial.ptrw(), records.ptr(), records.size() * sizeof(Record));
		buffer = rd->storage_buffer_create(initial.size(), initial);
		rd->set_resource_name(buffer, "GPU Instance Table");
		stats.grown = true;
		stats.records = records.size();
		for (uint32_t slot : dirty_slots) {
			dirty_flags[slot] = 0;
		}
		dirty_slots.clear();
		return stats;
	}

	if (dirty_slots.is_empty()) {
		return stats;
	}

	dirty_slots.sort();
	if (p_skip_one) {
		// Leave the first dirty record stale on the GPU (validation must notice).
		dirty_flags[dirty_slots[0]] = 0;
		dirty_slots.remove_at(0);
	}
	for (uint32_t i = 0; i < dirty_slots.size(); i++) {
		if (i == 0 || dirty_slots[i] != dirty_slots[i - 1] + 1) {
			stats.ranges++;
		}
	}
	stats.records = dirty_slots.size();
	if (stats.ranges > MAX_DIRECT_RANGES && _ensure_scatter(dirty_slots.size())) {
		_scatter(dirty_slots);
		stats.scattered = true;
		scattered_uploads++;
	} else {
		direct_uploads++;
		uint32_t i = 0;
		while (i < dirty_slots.size()) {
			const uint32_t from = dirty_slots[i];
			uint32_t to = from;
			while (i + 1 < dirty_slots.size() && dirty_slots[i + 1] == to + 1) {
				i++;
				to++;
			}
			rd->buffer_update(buffer, from * sizeof(Record), (to - from + 1) * sizeof(Record), records.ptr() + from);
			i++;
		}
	}
	for (uint32_t slot : dirty_slots) {
		dirty_flags[slot] = 0;
	}
	dirty_slots.clear();
	return stats;
}

bool GPUInstanceTable::_ensure_scatter(uint32_t p_count) {
	RD *rd = RD::get_singleton();
	if (scatter_version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		scatter_shader.initialize(modes);
		scatter_version = scatter_shader.version_create();
		scatter_pipeline = rd->compute_pipeline_create(scatter_shader.version_get_shader(scatter_version, 0));
	}
	if (scatter_pipeline.is_null()) {
		return false;
	}
	if (upload_capacity < p_count) {
		if (upload_records.is_valid()) {
			rd->free_rid(upload_records);
			rd->free_rid(upload_slots);
		}
		upload_capacity = MAX(MIN_CAPACITY, Math::next_power_of_2(p_count));
		upload_records = rd->storage_buffer_create(upload_capacity * sizeof(Record));
		upload_slots = rd->storage_buffer_create(upload_capacity * sizeof(uint32_t));
		rd->set_resource_name(upload_records, "GPU Instance Table Upload");
		rd->set_resource_name(upload_slots, "GPU Instance Table Upload Slots");
	}
	return upload_records.is_valid() && upload_slots.is_valid();
}

void GPUInstanceTable::_scatter(const LocalVector<uint32_t> &p_slots) {
	RD *rd = RD::get_singleton();
	const uint32_t count = p_slots.size();
	packed_records.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		packed_records[i] = records[p_slots[i]];
	}
	rd->buffer_update(upload_records, 0, count * sizeof(Record), packed_records.ptr());
	rd->buffer_update(upload_slots, 0, count * sizeof(uint32_t), p_slots.ptr());

	LocalVector<RD::Uniform> uniforms;
	const RID targets[3] = { buffer, upload_records, upload_slots };
	for (uint32_t binding = 0; binding < 3; binding++) {
		RD::Uniform uniform;
		uniform.binding = binding;
		uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		uniform.append_id(targets[binding]);
		uniforms.push_back(uniform);
	}
	const RID shader = scatter_shader.version_get_shader(scatter_version, 0);
	const uint32_t push[4] = { count, 0, 0, 0 };
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, scatter_pipeline);
	rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
	rd->compute_list_set_push_constant(compute, push, sizeof(push));
	rd->compute_list_dispatch(compute, (count + 63) / 64, 1, 1);
	rd->compute_list_end();
}

void GPUInstanceTable::request_validation(uint64_t p_frame) {
	if (validation_pending || buffer.is_null() || records.is_empty()) {
		return;
	}
	validation_pending = true;
	validation_frame = p_frame;
	validation_snapshot = records;
	validation_direct = direct_uploads;
	validation_scattered = scattered_uploads;
	direct_uploads = 0;
	scattered_uploads = 0;
	validating_table = this;
	RD::get_singleton()->buffer_get_data_async(buffer, callable_mp_static(&GPUInstanceTable::_validation_received), 0, records.size() * sizeof(Record));
}

void GPUInstanceTable::_validation_received(const Vector<uint8_t> &p_data) {
	GPUInstanceTable *table = validating_table;
	if (table == nullptr) {
		return;
	}
	table->validation_pending = false;
	const LocalVector<Record> &validation_snapshot = table->validation_snapshot;
	const uint64_t validation_frame = table->validation_frame;
	const uint32_t count = validation_snapshot.size();
	if (uint32_t(p_data.size()) != count * sizeof(Record)) {
		ERR_PRINT(vformat("GPU instance table check (frame %d): read %d bytes, expected %d.", validation_frame, p_data.size(), count * sizeof(Record)));
		return;
	}
	const Record *gpu = reinterpret_cast<const Record *>(p_data.ptr());
	uint32_t live = 0;
	for (uint32_t slot = 0; slot < count; slot++) {
		if (memcmp(&gpu[slot], &validation_snapshot[slot], sizeof(Record)) != 0) {
			ERR_PRINT(vformat("GPU instance table check (frame %d): slot %d differs from the CPU mirror (GPU generation %d, CPU generation %d).", validation_frame, slot, gpu[slot].generation, validation_snapshot[slot].generation));
			return;
		}
		live += validation_snapshot[slot].generation & 1;
	}
	print_line(vformat("GPU_INSTANCE_TABLE_CHECK: ok frame=%d slots=%d live=%d direct_uploads=%d scattered_uploads=%d", validation_frame, count, live, table->validation_direct, table->validation_scattered));
}

void GPUInstanceTable::free_buffer() {
	RD *rd = RD::get_singleton();
	if (buffer.is_valid()) {
		rd->free_rid(buffer);
		buffer = RID();
	}
	buffer_capacity = 0;
	if (upload_records.is_valid()) {
		rd->free_rid(upload_records);
		rd->free_rid(upload_slots);
		upload_records = RID();
		upload_slots = RID();
	}
	upload_capacity = 0;
}

GPUInstanceTable::~GPUInstanceTable() {
	if (validating_table == this) {
		validating_table = nullptr;
	}
	free_buffer();
	if (scatter_version.is_valid()) {
		RD *rd = RD::get_singleton();
		if (scatter_pipeline.is_valid()) {
			rd->free_rid(scatter_pipeline);
		}
		scatter_shader.version_free(scatter_version);
	}
}
