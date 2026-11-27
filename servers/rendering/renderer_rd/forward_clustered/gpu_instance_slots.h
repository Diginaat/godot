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
// The lowest free slot is reused first, so after a large scene is freed the
// slots of the next one start at 0 again and the table and the per-slot
// buffers can shrink to `get_capacity()`, one past the highest slot in use.
// Each slot has a generation that changes when it is freed, so data written
// for an earlier owner can be told apart from the current one. Generations
// are kept for every slot ever handed out, also past `get_capacity()`.
class GPUInstanceSlots {
	LocalVector<uint32_t> free_words; // One bit per slot: set while free.
	LocalVector<uint32_t> generations; // Even: free, odd: in use.
	uint32_t live_count = 0;
	uint32_t lowest_free = 0; // No free slot below this one.
	uint32_t high = 0; // One past the highest slot in use.

	bool _is_free(uint32_t p_slot) const { return free_words[p_slot >> 5] & (1u << (p_slot & 31)); }

public:
	static constexpr uint32_t INVALID = UINT32_MAX;

	uint32_t allocate() {
		uint32_t slot = generations.size();
		for (uint32_t word = lowest_free >> 5; word < free_words.size(); word++) {
			uint32_t bits = free_words[word];
			if (bits) {
				uint32_t bit = 0;
				while (!(bits & 1u)) {
					bits >>= 1;
					bit++;
				}
				slot = (word << 5) + bit;
				break;
			}
		}
		if (slot == generations.size()) {
			generations.push_back(0);
			if ((slot >> 5) >= free_words.size()) {
				free_words.push_back(0);
			}
		} else {
			free_words[slot >> 5] &= ~(1u << (slot & 31));
		}
		lowest_free = slot + 1;
		generations[slot]++;
		live_count++;
		high = MAX(high, slot + 1);
		return slot;
	}

	void free(uint32_t p_slot) {
		ERR_FAIL_UNSIGNED_INDEX(p_slot, generations.size());
		ERR_FAIL_COND_MSG(!(generations[p_slot] & 1), "GPU instance slot freed twice.");
		generations[p_slot]++;
		free_words[p_slot >> 5] |= 1u << (p_slot & 31);
		lowest_free = MIN(lowest_free, p_slot);
		live_count--;
		while (high > 0 && _is_free(high - 1)) {
			high--;
		}
	}

	bool is_allocated(uint32_t p_slot) const {
		return p_slot < generations.size() && (generations[p_slot] & 1);
	}

	uint32_t get_generation(uint32_t p_slot) const {
		ERR_FAIL_UNSIGNED_INDEX_V(p_slot, generations.size(), 0);
		return generations[p_slot];
	}

	// One past the highest slot in use: the table size needed for every live slot.
	uint32_t get_capacity() const { return high; }
	uint32_t get_live_count() const { return live_count; }
};
