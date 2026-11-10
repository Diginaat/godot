/**************************************************************************/
/*  core_templates_bench.cpp                                              */
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

// Standalone microbenchmark of actual Godot containers, not substitutes.
// Built by core_templates_bench.py against HEAD and working-tree sources.
// Uses actual memory.cpp/hashfuncs.cpp and verbatim required String methods;
// only diagnostics are stubbed. See the runner's extraction and source manifest.

#include "core/string/ustring.h"
#include "core/templates/a_hash_map.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

// These specializations are normally explicitly instantiated by a_hash_map.cpp.
template class AHashMap<int, int>;
template class AHashMap<String, int>;

// Any diagnostic means the benchmark is invalid. No engine/OS initialization is
// needed for these workloads, and no warning/error is silently swallowed.
static void bench_fail(const char *p_message) {
	std::fprintf(stderr, "BENCH_FAIL: %s\n", p_message);
	std::exit(2);
}
static void check(bool p_condition) {
	if (!p_condition) {
		bench_fail("correctness check");
	}
}
bool is_print_verbose_enabled() {
	return false;
}
void _err_flush_stdout() {
	std::fflush(stdout);
}
void _err_print_error(const char *, const char *, int, const char *p_error, bool, ErrorHandlerType) {
	bench_fail(p_error);
}
void _err_print_error(const char *, const char *, int, const char *p_error, const char *, bool, ErrorHandlerType) {
	bench_fail(p_error);
}
void _err_print_error(const char *, const char *, int, const String &, bool, ErrorHandlerType) {
	bench_fail("String diagnostic");
}
void _err_print_error(const char *, const char *, int, const String &, const char *, bool, ErrorHandlerType) {
	bench_fail("String diagnostic");
}
void _err_print_error(const char *, const char *, int, const char *, const String &, bool, ErrorHandlerType) {
	bench_fail("String diagnostic");
}
void _err_print_error(const char *, const char *, int, const String &, const String &, bool, ErrorHandlerType) {
	bench_fail("String diagnostic");
}
void _err_print_index_error(const char *, const char *, int, int64_t, int64_t, const char *, const char *, const char *, bool, bool) {
	bench_fail("index diagnostic");
}
void _err_print_index_error(const char *, const char *, int, int64_t, int64_t, const char *, const char *, const String &, bool, bool) {
	bench_fail("index diagnostic");
}

namespace {
using Clock = std::chrono::steady_clock;
volatile uint64_t sink = 0;
constexpr int N = 32768;
constexpr int PASSES = 32;

struct Sample {
	const char *name;
	uint64_t operations;
	uint64_t checksum = 0;
	uint64_t live_bytes = 0;
	Clock::time_point start;
	Sample(const char *p_name, uint64_t p_operations) : name(p_name), operations(p_operations), start(Clock::now()) {}
	void finish() {
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const double ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
		check(ns > 0.0 && operations > 0);
		sink = checksum;
		std::printf("%s,%.6f,%llu,%llu,%llu\n", name, ns / operations,
				static_cast<unsigned long long>(operations), static_cast<unsigned long long>(live_bytes), static_cast<unsigned long long>(checksum));
	}
};

std::vector<int> shuffled(int p_size) {
	std::vector<int> result(p_size);
	std::iota(result.begin(), result.end(), 0);
	uint32_t state = 0x7137;
	for (int i = p_size - 1; i > 0; i--) {
		state = state * 1664525U + 1013904223U;
		std::swap(result[i], result[state % (i + 1)]);
	}
	return result;
}

template <typename Map>
void bench_map(const char *p_insert, const char *p_hit, const char *p_miss, const char *p_churn, const std::vector<int> &p_queries) {
	const uint64_t base_bytes = Memory::get_mem_usage();
	{
		Sample sample(p_insert, uint64_t(N) * 8);
		for (int repeat = 0; repeat < 8; repeat++) {
			Map map;
			for (int key : p_queries) {
				map.insert(key, key * 3);
			}
			sample.live_bytes = Memory::get_mem_usage() - base_bytes;
			sample.checksum += map.size();
		}
		sample.finish();
		check(sample.checksum == uint64_t(N) * 8);
	}
	{
		Map map;
		for (int key : p_queries) {
			map.insert(key, key * 3);
		}
		Sample hit(p_hit, uint64_t(N) * PASSES);
		hit.live_bytes = Memory::get_mem_usage() - base_bytes;
		for (int pass = 0; pass < PASSES; pass++) {
			std::atomic_signal_fence(std::memory_order_seq_cst);
			for (int i = 0; i < N; i++) {
				hit.checksum += map.get(p_queries[(i + pass * 97) & (N - 1)]);
			}
		}
		hit.finish();
		check(hit.checksum == uint64_t(N) * (N - 1) / 2 * 3 * PASSES);
		Sample miss(p_miss, uint64_t(N) * PASSES);
		miss.live_bytes = hit.live_bytes;
		for (int pass = 0; pass < PASSES; pass++) {
			std::atomic_signal_fence(std::memory_order_seq_cst);
			for (int i = 0; i < N; i++) {
				miss.checksum += map.has(p_queries[(i + pass * 97) & (N - 1)] + N);
			}
		}
		miss.finish();
		check(miss.checksum == 0);
		Sample churn(p_churn, uint64_t(N) * 4 * 2);
		for (int pass = 0; pass < 4; pass++) {
			for (int key : p_queries) {
				check(map.erase(key + pass * N));
				map.insert(key + (pass + 1) * N, key * 3);
			}
		}
		churn.live_bytes = Memory::get_mem_usage() - base_bytes;
		churn.checksum = map.size();
		churn.finish();
		check(map.size() == N);
		for (int key : p_queries) {
			check(map.get(key + 4 * N) == key * 3);
		}
	}
	check(Memory::get_mem_usage() == base_bytes);
}

void bench_set(const std::vector<int> &p_queries) {
	const uint64_t base = Memory::get_mem_usage();
	{
		Sample insert("hashset_insert", uint64_t(N) * 8);
		for (int repeat = 0; repeat < 8; repeat++) {
			HashSet<int> set;
			for (int key : p_queries) {
				set.insert(key);
			}
			insert.live_bytes = Memory::get_mem_usage() - base;
			insert.checksum += set.size();
		}
		insert.finish();
		check(insert.checksum == uint64_t(N) * 8);
	}
	{
		HashSet<int> set;
		for (int key : p_queries) {
			set.insert(key);
		}
		Sample hit("hashset_hit", uint64_t(N) * PASSES);
		hit.live_bytes = Memory::get_mem_usage() - base;
		for (int pass = 0; pass < PASSES; pass++) {
			std::atomic_signal_fence(std::memory_order_seq_cst);
			for (int i = 0; i < N; i++) {
				hit.checksum += set.has(p_queries[(i + pass * 97) & (N - 1)]);
			}
		}
		hit.finish();
		check(hit.checksum == uint64_t(N) * PASSES);
		Sample churn("hashset_churn", uint64_t(N) * 4 * 2);
		for (int pass = 0; pass < 4; pass++) {
			for (int key : p_queries) {
				check(set.erase(key + pass * N));
				set.insert(key + (pass + 1) * N);
			}
		}
		churn.live_bytes = Memory::get_mem_usage() - base;
		churn.checksum = set.size();
		churn.finish();
		for (int key : p_queries) {
			check(set.has(key + 4 * N));
		}
	}
	check(Memory::get_mem_usage() == base);
}

void bench_vector() {
	const uint64_t base = Memory::get_mem_usage();
	{
		Sample append("vector_append", uint64_t(N) * 32);
		for (int repeat = 0; repeat < 32; repeat++) {
			Vector<int> values;
			for (int i = 0; i < N; i++) {
				check(!values.push_back(i));
			}
			append.live_bytes = Memory::get_mem_usage() - base;
			append.checksum += values[N - 1];
		}
		append.finish();
		check(append.checksum == uint64_t(N - 1) * 32);
	}
	{
		Vector<int> original;
		check(original.resize(4096) == OK);
		original.fill(7);
		Sample copy("vector_copy_detach_4096", 1024);
		for (int repeat = 0; repeat < 1024; repeat++) {
			Vector<int> values = original;
			values.write[repeat & 4095] = repeat;
			copy.checksum += values[repeat & 4095];
			copy.live_bytes = Memory::get_mem_usage() - base;
		}
		copy.finish();
		check(copy.checksum == uint64_t(1023) * 1024 / 2);
		check(original[0] == 7);
	}
	{
		const auto order = shuffled(4096);
		Sample sort("vector_sort_unvalidated_4096", 128);
		for (int repeat = 0; repeat < 128; repeat++) {
			Vector<int> values;
			check(values.resize(4096) == OK);
			std::copy(order.begin(), order.end(), values.ptrw());
			values.sort_custom<Comparator<int>, false>();
			check(values[0] == 0 && values[4095] == 4095);
			sort.checksum += values[4095];
			sort.live_bytes = Memory::get_mem_usage() - base;
		}
		sort.finish();
		check(sort.checksum == uint64_t(4095) * 128);
	}
	check(Memory::get_mem_usage() == base);
}

void bench_strings(int p_length, const char *p_copy_name, const char *p_map_name) {
	const uint64_t base = Memory::get_mem_usage();
	{
		String original;
		check(original.resize_uninitialized(p_length + 1) == OK);
		std::fill_n(original.ptrw(), p_length, U'x');
		original.ptrw()[p_length] = 0;
		Sample copy(p_copy_name, 32768);
		for (int i = 0; i < 32768; i++) {
			String value = original;
			value[0] = char32_t(U'a' + i % 26);
			copy.checksum += value.ptr()[0];
			copy.live_bytes = Memory::get_mem_usage() - base;
		}
		copy.finish();
		check(original.ptr()[0] == U'x');
	}
	{
		std::vector<String> keys(4096);
		for (int key = 0; key < 4096; key++) {
			check(keys[key].resize_uninitialized(p_length + 1) == OK);
			char32_t *dst = keys[key].ptrw();
			std::fill_n(dst, p_length, U'x');
			for (int i = 0; i < 8; i++) {
				dst[i] = U'A' + ((key >> (i * 2)) & 3);
			}
			dst[p_length] = 0;
		}
		const uint64_t keys_bytes = Memory::get_mem_usage();
		AHashMap<String, int> map;
		for (int key = 0; key < 4096; key++) {
			map.insert(keys[key], key);
		}
		const auto order = shuffled(4096);
		Sample hit(p_map_name, uint64_t(4096) * 32);
		hit.live_bytes = Memory::get_mem_usage() - keys_bytes;
		for (int pass = 0; pass < 32; pass++) {
			std::atomic_signal_fence(std::memory_order_seq_cst);
			for (int i = 0; i < 4096; i++) {
				hit.checksum += map.get(keys[order[(i + pass * 97) & 4095]]);
			}
		}
		hit.finish();
		check(hit.checksum == uint64_t(4096) * 4095 / 2 * 32);
	}
	check(Memory::get_mem_usage() == base);
}

void bench_hash(size_t p_size, const char *p_name) {
	std::vector<uint8_t> data(p_size + 1);
	for (size_t i = 0; i < data.size(); i++) {
		data[i] = uint8_t(i * 37);
	}
	const uint64_t repeats = std::max<uint64_t>(4096, 16 * 1024 * 1024 / p_size);
	Sample hash(p_name, repeats);
	for (uint64_t i = 0; i < repeats; i++) {
#if BENCH_MODERN_HASH
		hash.checksum += hash_buffer(data.data() + 1, p_size, i);
#else
		hash.checksum += hash_murmur3_buffer(data.data() + 1, int(p_size), uint32_t(i));
#endif
	}
	hash.finish();
	check(hash.checksum != 0);
}
} // namespace

int main() {
	check(Memory::get_mem_usage() == 0);
	std::puts("case,ns_per_op,operations,live_bytes,checksum");
	const auto queries = shuffled(N);
	bench_map<HashMap<int, int>>("hashmap_insert", "hashmap_hit", "hashmap_miss", "hashmap_churn", queries);
	bench_map<AHashMap<int, int>>("ahashmap_insert", "ahashmap_hit", "ahashmap_miss", "ahashmap_churn", queries);
	bench_set(queries);
	bench_vector();
	bench_strings(16, "string_copy_detach_16", "ahashmap_string_hit_16");
	bench_strings(128, "string_copy_detach_128", "ahashmap_string_hit_128");
	bench_hash(8, "hash_bytes_8");
	bench_hash(32, "hash_bytes_32");
	bench_hash(128, "hash_bytes_128");
	bench_hash(256, "hash_bytes_256");
	bench_hash(4096, "hash_bytes_4096");
	bench_hash(65536, "hash_bytes_65536");
	check(Memory::get_mem_usage() == 0);
	return 0;
}
