/**************************************************************************/
/*  array_bench_counters.h                                                */
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

// Benchmark-only CRT hooks. Force-included ONLY when compiling memory.cpp.
// Timing builds leave the allocator uninstrumented; count builds are untimed.
#pragma once
#include "core/os/memory.h"

#include <malloc.h>

#include <cstdint>
#include <cstdlib>

namespace ArrayBench {
inline uint64_t allocations = 0, reallocations = 0, frees = 0;
inline uint64_t live = 0, peak = 0;
inline void reset() {
	allocations = reallocations = frees = 0;
	peak = live;
}
inline void add(void *p) {
	if (p) {
		live += _msize(p);
		if (live > peak) {
			peak = live;
		}
	}
}
inline void *allocate(size_t size) {
	allocations++;
	void *p = std::malloc(size);
	add(p);
	return p;
}
inline void *allocate_zero(size_t count, size_t size) {
	allocations++;
	void *p = std::calloc(count, size);
	add(p);
	return p;
}
inline void *reallocate(void *old, size_t size) {
	reallocations++;
	const size_t previous = old ? _msize(old) : 0;
	void *p = std::realloc(old, size);
	if (p || !size) {
		live -= previous;
		add(p);
	}
	return p;
}
inline void release(void *p) {
	if (p) {
		frees++;
		live -= _msize(p);
	}
	std::free(p);
}
} // namespace ArrayBench

#if defined(ARRAY_BENCH_HOOKS) && ARRAY_BENCH_HOOKS
#define malloc ArrayBench::allocate
#define calloc ArrayBench::allocate_zero
#define realloc ArrayBench::reallocate
#define free ArrayBench::release
#endif
