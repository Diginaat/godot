/**************************************************************************/
/*  gpu_instance_table.h                                                  */
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

#include "core/templates/local_vector.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/gpu_instance_scatter.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

// Persistent per-instance GPU data for Forward+ (docs/renderer/roadmap.md,
// Phase 1, decision D026). One record per GPUInstanceSlots slot, kept in a
// CPU mirror and uploaded only where it changed. Holds what stays the same
// between views and passes; per-view values (fade, motion, GI indices) and
// per-surface values (compressed AABB, UV scale) stay out of it.
class GPUInstanceTable {
public:
	struct Record {
		float transform[12]; // Transposed 3x4, as in the per-frame instance data.
		float prev_transform[12];
		uint32_t flags; // Instance base flags plus non-uniform scale.
		uint32_t instance_uniforms_ofs;
		uint32_t layer_mask;
		uint32_t generation; // Slot generation; odd while the slot is in use.
		float lightmap_uv_scale[4];
	};
	static_assert(sizeof(Record) == 128, "GPUInstanceTable::Record must stay 128 bytes (std430 layout).");

	static constexpr uint32_t MIN_CAPACITY = 1024;
	// Up to this many contiguous runs are written with one buffer_update
	// each; more go through one packed upload and a compute scatter.
	static constexpr uint32_t MAX_DIRECT_RANGES = 16;

private:
	LocalVector<Record> records; // CPU mirror, indexed by slot.
	LocalVector<uint8_t> dirty_flags;
	LocalVector<uint32_t> dirty_slots;
	RID buffer;
	uint32_t buffer_capacity = 0; // Records the GPU buffer holds.

	// Packed upload for the scatter path.
	GpuInstanceScatterShaderRD scatter_shader;
	RID scatter_version;
	RID scatter_pipeline;
	RID upload_records;
	RID upload_slots;
	uint32_t upload_capacity = 0;
	LocalVector<Record> packed_records;
	bool _ensure_scatter(uint32_t p_count);
	void _scatter(const LocalVector<uint32_t> &p_slots);

	// Validation (rendering/geometry/gpu_instance_table/validate).
	bool validation_pending = false;
	LocalVector<Record> validation_snapshot;
	uint64_t validation_frame = 0;
	// Uploads since the last requested check, by path (reported with it).
	uint32_t direct_uploads = 0;
	uint32_t scattered_uploads = 0;
	uint32_t validation_direct = 0;
	uint32_t validation_scattered = 0;
	// The readback callback finds the table through this (cleared when the
	// table is destroyed, so a late callback does nothing).
	static GPUInstanceTable *validating_table;
	static void _validation_received(const Vector<uint8_t> &p_data);

public:
	struct UploadStats {
		uint32_t records = 0; // Dirty records written.
		uint32_t ranges = 0; // Contiguous runs of dirty records.
		bool scattered = false; // Packed upload and compute scatter used.
		bool grown = false;
	};

	void ensure_slot(uint32_t p_slot);
	void mark_dirty(uint32_t p_slot);
	bool has_dirty() const { return !dirty_slots.is_empty(); }
	const LocalVector<uint32_t> &get_dirty_slots() const { return dirty_slots; }
	void set_record(uint32_t p_slot, const Record &p_record);
	const Record &get_record(uint32_t p_slot) const { return records[p_slot]; }
	uint32_t get_slot_count() const { return records.size(); }

	// Writes every dirty record to the GPU buffer, growing it when needed.
	// p_skip_one drops one dirty record (validation self-test only).
	UploadStats upload(bool p_skip_one = false);
	// Reads the buffer back without stalling and compares it with the mirror
	// as uploaded this frame; prints the result. At most one check in flight.
	void request_validation(uint64_t p_frame);

	RID get_buffer() const { return buffer; }
	uint64_t get_gpu_bytes() const { return uint64_t(buffer_capacity) * sizeof(Record); }
	void free_buffer();

	~GPUInstanceTable();
};
