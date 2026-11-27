/**************************************************************************/
/*  test_gpu_instance_slots.cpp                                           */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_gpu_instance_slots)

#ifdef RD_ENABLED

#include "servers/rendering/renderer_rd/forward_clustered/gpu_instance_slots.h"

namespace TestGPUInstanceSlots {

TEST_CASE("[GPUInstanceSlots] Slots are dense and stable") {
	GPUInstanceSlots slots;
	for (uint32_t i = 0; i < 8; i++) {
		CHECK(slots.allocate() == i);
	}
	CHECK(slots.get_capacity() == 8);
	CHECK(slots.get_live_count() == 8);
	for (uint32_t i = 0; i < 8; i++) {
		CHECK(slots.is_allocated(i));
	}
	CHECK_FALSE(slots.is_allocated(8));
	CHECK_FALSE(slots.is_allocated(GPUInstanceSlots::INVALID));
}

TEST_CASE("[GPUInstanceSlots] Freed slots are reused before the table grows") {
	GPUInstanceSlots slots;
	for (uint32_t i = 0; i < 4; i++) {
		slots.allocate();
	}
	slots.free(1);
	slots.free(3);
	CHECK_FALSE(slots.is_allocated(1));
	CHECK(slots.get_live_count() == 2);
	// Lowest first.
	CHECK(slots.allocate() == 1);
	CHECK(slots.allocate() == 3);
	CHECK(slots.allocate() == 4);
	CHECK(slots.get_capacity() == 5);
}

TEST_CASE("[GPUInstanceSlots] Capacity shrinks to the highest slot in use") {
	GPUInstanceSlots slots;
	for (uint32_t i = 0; i < 100; i++) {
		slots.allocate();
	}
	slots.free(99);
	CHECK(slots.get_capacity() == 99);
	for (uint32_t i = 0; i < 50; i++) {
		slots.free(i);
	}
	// Slot 98 still holds the table at 99.
	CHECK(slots.get_capacity() == 99);
	for (uint32_t i = 50; i < 99; i++) {
		slots.free(i);
	}
	CHECK(slots.get_capacity() == 0);
	CHECK(slots.get_live_count() == 0);
	// The next scene starts at 0 again, and generations go on.
	for (uint32_t i = 0; i < 3; i++) {
		CHECK(slots.allocate() == i);
	}
	CHECK(slots.get_capacity() == 3);
	CHECK(slots.get_generation(0) == 3);
	CHECK(slots.get_generation(99) == 2);
	CHECK_FALSE(slots.is_allocated(99));
}

TEST_CASE("[GPUInstanceSlots] Generations change on every free and reuse") {
	GPUInstanceSlots slots;
	const uint32_t slot = slots.allocate();
	const uint32_t first = slots.get_generation(slot);
	CHECK((first & 1) == 1);
	slots.free(slot);
	CHECK((slots.get_generation(slot) & 1) == 0);
	CHECK(slots.allocate() == slot);
	CHECK(slots.get_generation(slot) == first + 2);
}

TEST_CASE("[GPUInstanceSlots] Churn never needs more than the peak") {
	GPUInstanceSlots slots;
	LocalVector<uint32_t> live;
	uint32_t peak = 0;
	uint32_t state = 12345;
	for (uint32_t step = 0; step < 20000; step++) {
		state = state * 1664525u + 1013904223u;
		if (live.is_empty() || (state >> 16) % 3 != 0) {
			live.push_back(slots.allocate());
			// Freed slots are reused before new ones: never past the peak.
			CHECK(live[live.size() - 1] <= peak);
		} else {
			const uint32_t pick = (state >> 8) % live.size();
			slots.free(live[pick]);
			live[pick] = live[live.size() - 1];
			live.resize(live.size() - 1);
		}
		peak = MAX(peak, live.size());
		CHECK(slots.get_live_count() == live.size());
	}
	uint32_t highest = 0;
	for (uint32_t slot : live) {
		CHECK(slots.is_allocated(slot));
		highest = MAX(highest, slot + 1);
	}
	CHECK(slots.get_capacity() == highest);
	CHECK(slots.get_capacity() <= peak);
}

TEST_CASE("[GPUInstanceSlots] Double free is rejected") {
	GPUInstanceSlots slots;
	const uint32_t slot = slots.allocate();
	slots.free(slot);
	ERR_PRINT_OFF;
	slots.free(slot);
	slots.free(100);
	ERR_PRINT_ON;
	CHECK(slots.get_live_count() == 0);
	CHECK(slots.allocate() == slot);
	CHECK(slots.get_capacity() == 1);
}

} // namespace TestGPUInstanceSlots

#endif // RD_ENABLED
