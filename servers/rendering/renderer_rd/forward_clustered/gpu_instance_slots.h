/**************************************************************************/
/*  gpu_instance_slots.h                                                  */
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

#include "core/error/error_macros.h"
#include "core/templates/local_vector.h"

// Stable slots for geometry instances in the GPU instance table
// (docs/renderer/roadmap.md, Phase 1). A slot is an index into a persistent
// per-instance GPU buffer; it stays the same for the instance's lifetime.
// Freed slots are reused last-in first-out, so the table never needs more
// entries than the peak number of live instances (`get_capacity()`).
// Each slot has a generation that changes when it is freed, so data written
// for an earlier owner can be told apart from the current one.
class GPUInstanceSlots {
	LocalVector<uint32_t> free_slots;
	LocalVector<uint32_t> generations; // Even: free, odd: in use.
	uint32_t live_count = 0;

public:
	static constexpr uint32_t INVALID = UINT32_MAX;

	uint32_t allocate() {
		uint32_t slot;
		if (free_slots.is_empty()) {
			slot = generations.size();
			generations.push_back(0);
		} else {
			slot = free_slots[free_slots.size() - 1];
			free_slots.resize(free_slots.size() - 1);
		}
		generations[slot]++;
		live_count++;
		return slot;
	}

	void free(uint32_t p_slot) {
		ERR_FAIL_UNSIGNED_INDEX(p_slot, generations.size());
		ERR_FAIL_COND_MSG(!(generations[p_slot] & 1), "GPU instance slot freed twice.");
		generations[p_slot]++;
		free_slots.push_back(p_slot);
		live_count--;
	}

	bool is_allocated(uint32_t p_slot) const {
		return p_slot < generations.size() && (generations[p_slot] & 1);
	}

	uint32_t get_generation(uint32_t p_slot) const {
		ERR_FAIL_UNSIGNED_INDEX_V(p_slot, generations.size(), 0);
		return generations[p_slot];
	}

	// Number of slots ever handed out: the table size needed for every slot.
	uint32_t get_capacity() const { return generations.size(); }
	uint32_t get_live_count() const { return live_count; }
};
