/**************************************************************************/
/*  unique_array_bench.cpp                                                */
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

// Original Godot LocalVector versus an experimental std::vector allocator.
// Not an engine container replacement. Baseline sources are supplied by runner.
#include "array_bench_counters.h"

#include "core/templates/local_vector.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool value) {
	if (!value) {
		std::fputs("ARRAY_BENCH_FAIL\n", stderr);
		std::exit(2);
	}
}
bool is_print_verbose_enabled() {
	return false;
}
void _err_flush_stdout() {
	std::fflush(stdout);
}
void _err_print_error(const char *, const char *, int, const char *, bool, ErrorHandlerType) {
	require(false);
}
void _err_print_error(const char *, const char *, int, const char *, const char *, bool, ErrorHandlerType) {
	require(false);
}
void _err_print_index_error(const char *, const char *, int, int64_t, int64_t, const char *, const char *, const char *, bool, bool) {
	require(false);
}

template <typename T>
struct GodotAllocator {
	using value_type = T;
	GodotAllocator() = default;
	template <typename U>
	GodotAllocator(const GodotAllocator<U> &) {}
	T *allocate(size_t count) {
		static_assert(alignof(T) <= Memory::MAX_ALIGN, "Overaligned types not evaluated.");
		require(count <= SIZE_MAX / sizeof(T));
		T *p = static_cast<T *>(Memory::alloc_static(count * sizeof(T)));
		require(p != nullptr);
		return p;
	}
	void deallocate(T *p, size_t) { Memory::free_static(p); }
	template <typename U>
	bool operator==(const GodotAllocator<U> &) const { return true; }
	template <typename U>
	bool operator!=(const GodotAllocator<U> &) const { return false; }
};
template <typename T>
using StdArray = std::vector<T, GodotAllocator<T>>;

struct Tracked {
	inline static int live = 0;
	int value = 0;
	Tracked() { live++; }
	Tracked(const Tracked &other) : value(other.value) { live++; }
	Tracked(Tracked &&other) noexcept : value(other.value) { live++; }
	Tracked &operator=(const Tracked &) = default;
	Tracked &operator=(Tracked &&) = default;
	~Tracked() { live--; }
};

static void contracts() {
	require(Memory::get_mem_usage() == 0);
	{
		LocalVector<Tracked> original;
		original.resize(16);
		original[0].value = 7;
		LocalVector<Tracked> copy(original);
		require(Tracked::live == 32 && original.ptr() != copy.ptr());
		copy[0].value = 8;
		require(original[0].value == 7);
		Tracked *ptr = copy.ptr();
		LocalVector<Tracked> moved(std::move(copy));
		require(copy.ptr() == nullptr && moved.ptr() == ptr);
		const auto capacity = moved.get_capacity();
		moved.clear();
		require(moved.get_capacity() == capacity && Tracked::live == 16);
		moved.reset();
		require(moved.ptr() == nullptr && moved.get_capacity() == 0);
	}
	require(Tracked::live == 0);
	{
		StdArray<Tracked> original(16);
		original[0].value = 7;
		StdArray<Tracked> copy(original);
		require(Tracked::live == 32 && original.data() != copy.data());
		copy[0].value = 8;
		require(original[0].value == 7);
		Tracked *ptr = copy.data();
		StdArray<Tracked> moved(std::move(copy));
		require(moved.data() == ptr);
		const auto capacity = moved.capacity();
		moved.clear();
		require(moved.capacity() == capacity && Tracked::live == 16);
	}
	require(Tracked::live == 0);
	{
		LocalVector<int> local;
		local.reserve(2);
		local.push_back(7);
		local.push_back(8);
		local.push_back(local[0]);
		require(local[2] == 7);
		local.resize_initialized(16);
		for (int i = 3; i < 16; i++) {
			require(local[i] == 0);
		}
		Vector<int> shared{ 1, 2, 3 };
		Vector<int> copy = shared;
		require(copy.ptr() == shared.ptr());
		copy.write[0] = 9;
		require(copy.ptr() != shared.ptr() && shared[0] == 1);
	}
	require(Memory::get_mem_usage() == 0 && ArrayBench::live == 0);
}

static volatile uint64_t sink = 0;
template <typename Array>
size_t capacity(const Array &a) {
	return a.capacity();
}
template <typename T>
size_t capacity(const LocalVector<T> &a) {
	return a.get_capacity();
}
template <typename Array>
void initialized(Array &a, size_t n) {
	a.resize(n);
}
template <typename T>
void initialized(LocalVector<T> &a, size_t n) {
	a.resize_initialized(n);
}
template <typename Array>
const int *data(const Array &a) {
	return a.data();
}
template <typename T>
const T *data(const LocalVector<T> &a) {
	return a.ptr();
}
template <typename Array>
void pop(Array &a) {
	a.pop_back();
}
template <typename T>
void pop(LocalVector<T> &a) {
	a.remove_at(a.size() - 1);
}

template <typename Array>
void sample(const char *variant, int operation, size_t n, int index) {
#if ARRAY_BENCH_SPECIALIZE_COPY
	// Diagnostic mode: remove unrelated workload control flow from both builds.
	operation = 3;
#endif
	const char *names[] = { "append", "reserve_append", "resize_initialized", "copy", "copy_then_move", "iterate", "random_access", "copy_then_pop_all" };
	Array original;
	if (operation >= 3) {
		for (size_t i = 0; i < n; i++) {
			original.push_back(int(i));
		}
	}
#if ARRAY_BENCH_COPY_ONLY
	const size_t work = 67108864;
#else
	const size_t work = 1048576;
#endif
	const size_t repeats = std::max<size_t>(1, std::min<size_t>(32768, work / std::max<size_t>(1, n)));
	const uint64_t base = ArrayBench::live;
#if ARRAY_BENCH_COUNT_RUN
	const uint64_t fixture_requested = Memory::get_mem_usage();
#endif
	ArrayBench::reset();
	uint64_t checksum = 0, retained_bytes = 0, cap = 0;
	const auto start = std::chrono::steady_clock::now();
	for (size_t repeat = 0; repeat < repeats; repeat++) {
		std::atomic_signal_fence(std::memory_order_seq_cst);
		if (operation <= 2) {
			Array a;
			if (operation == 2) {
				initialized(a, n);
				if (n) {
					require(a[n - 1] == 0);
				}
			} else {
				if (operation == 1) {
					a.reserve(n);
				}
				for (size_t i = 0; i < n; i++) {
					a.push_back(int(i));
				}
			}
			cap = capacity(a);
			retained_bytes = Memory::get_mem_usage();
			checksum += a.size() + (n ? a[n - 1] : 0);
		} else if (operation == 3 || operation == 4) {
			Array a(original);
#if ARRAY_BENCH_COUNT_RUN
			cap = capacity(a);
			retained_bytes = Memory::get_mem_usage() - fixture_requested;
#endif
			if (operation == 4) {
				Array moved(std::move(a));
				require(data(moved) != nullptr || n == 0);
				checksum += moved.size();
			} else {
				checksum += a.size();
			}
		} else if (operation == 5 || operation == 6) {
			uint64_t sum = 0;
			for (size_t i = 0; i < n; i++) {
				// Odd multiplier permutes the power-of-two datasets without allocation.
				const size_t position = operation == 5 ? i : ((i * 65537 + repeat) & (n - 1));
				sum += original[position];
			}
			checksum += sum;
		} else {
			Array a(original);
			while (a.size()) {
				pop(a);
			}
			checksum += a.size();
		}
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / repeats;
	sink = checksum;
	uint64_t expected = 0;
	if (operation <= 1) {
		expected = n + (n ? n - 1 : 0);
	} else if (operation <= 4) {
		expected = n;
	} else if (operation <= 6) {
		expected = uint64_t(n) * (n ? n - 1 : 0) / 2;
	}
	require(checksum == expected * repeats);
	if (index >= 0) {
		std::printf("%s,%s,%zu,%d,%.6f,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", variant, names[operation], n, index, ns, repeats,
				(unsigned long long)checksum, (unsigned long long)ArrayBench::allocations, (unsigned long long)ArrayBench::reallocations,
				(unsigned long long)ArrayBench::frees, (unsigned long long)(ArrayBench::peak - base), (unsigned long long)retained_bytes, (unsigned long long)cap);
	}
	require(ArrayBench::live == base);
}

int main() {
	contracts();
	std::puts("variant,case,n,sample,ns_per_cycle,repeats,checksum,allocations,reallocations,frees,crt_peak_delta,live_requested_bytes,capacity");
#if ARRAY_BENCH_BOUNDARY
	const size_t sizes[] = { 0, 1, 16, 64, 128, 255, 256, 257, 512, 4096, 32768, 1048576 };
#else
	const size_t sizes[] = { 0, 1, 16, 256, 4096, 32768, 1048576 };
#endif
	for (int index = -1; index < ARRAY_BENCH_SAMPLES; index++) {
		for (size_t n : sizes) {
			for (int operation = 0; operation < 8; operation++) {
#if ARRAY_BENCH_COPY_ONLY
				if (operation != 3) {
					continue;
				}
#endif
				if (index % 2) {
					sample<LocalVector<int>>("local", operation, n, index);
					sample<StdArray<int>>("stl", operation, n, index);
				} else {
					sample<StdArray<int>>("stl", operation, n, index);
					sample<LocalVector<int>>("local", operation, n, index);
				}
			}
		}
	}
	require(Memory::get_mem_usage() == 0 && ArrayBench::live == 0);
}
