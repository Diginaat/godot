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
	uint32_t i = 0;
	while (i < dirty_slots.size()) {
		const uint32_t from = dirty_slots[i];
		uint32_t to = from;
		while (i + 1 < dirty_slots.size() && dirty_slots[i + 1] == to + 1) {
			i++;
			to++;
		}
		rd->buffer_update(buffer, from * sizeof(Record), (to - from + 1) * sizeof(Record), records.ptr() + from);
		stats.ranges++;
		stats.records += to - from + 1;
		i++;
	}
	for (uint32_t slot : dirty_slots) {
		dirty_flags[slot] = 0;
	}
	dirty_slots.clear();
	return stats;
}

void GPUInstanceTable::request_validation(uint64_t p_frame) {
	if (validation_pending || buffer.is_null() || records.is_empty()) {
		return;
	}
	validation_pending = true;
	validation_frame = p_frame;
	validation_snapshot = records;
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
	print_line(vformat("GPU_INSTANCE_TABLE_CHECK: ok frame=%d slots=%d live=%d", validation_frame, count, live));
}

void GPUInstanceTable::free_buffer() {
	if (buffer.is_valid()) {
		RD::get_singleton()->free_rid(buffer);
		buffer = RID();
	}
	buffer_capacity = 0;
}

GPUInstanceTable::~GPUInstanceTable() {
	if (validating_table == this) {
		validating_table = nullptr;
	}
	free_buffer();
}
