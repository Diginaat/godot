/**************************************************************************/
/*  test_hash_buffer.cpp                                                  */
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

TEST_FORCE_LINK(test_hash_buffer)

// Upstream also defines two non-inline prime helpers. Isolate the reference
// copy so it cannot collide with the production copy linked from hashfuncs.cpp.
namespace ReferenceWyhash {
#include "thirdparty/wyhash/wyhash.h"
}

#include "core/templates/a_hash_map.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

#define XXH_INLINE_ALL
#include "thirdparty/xxhash/xxhash.h"
#undef XXH_INLINE_ALL

namespace TestHashBuffer {

struct ConstantHasher {
	static uint32_t hash(int) { return 0; }
};

TEST_CASE("[Hashing] Byte hash dispatch matches upstream wyhash and XXH3") {
	uint8_t data[1025];
	for (size_t i = 0; i < sizeof(data); i++) {
		data[i] = uint8_t(i * 37);
	}
	const size_t lengths[] = { 0, 1, 3, 4, 7, 8, 15, 16, 17, 47, 48, 49, 127, 255, 256, 257, 1024 };
	for (uint64_t seed : { uint64_t(0), uint64_t(42), UINT64_MAX }) {
		for (size_t length : lengths) {
			// Offset one byte exercises unaligned reads as well as threshold edges.
			const void *ptr = data + 1;
			const uint64_t expected = length < HASH_XXH3_THRESHOLD ? ReferenceWyhash::wyhash(ptr, length, seed, ReferenceWyhash::_wyp) : XXH3_64bits_withSeed(ptr, length, seed);
			CHECK(hash_buffer64(ptr, length, seed) == expected);
			CHECK(hash_buffer(ptr, length, seed) == (uint32_t(expected) ^ uint32_t(expected >> 32)));
		}
	}
	CHECK(hash_buffer64(nullptr, 0) == ReferenceWyhash::wyhash("", 0, 0, ReferenceWyhash::_wyp));
}

TEST_CASE("[Hashing] Public hashes keep their outputs") {
	CHECK(hash_murmur3_buffer("foo", 3, 0) == 0xf6a5c420U);
	CHECK(HashMapHasherDefault::hash(uint64_t(42)) == hash_one_uint64(42));
	CHECK(HashMapHasherDefault::hash(uint32_t(42)) == hash_fmix32(42));
	CHECK(HashMapHasherDefault::hash("foo") == hash_djb2("foo"));
	const String text = "public hash stays stable";
	CHECK(HashMapHasherDefault::hash(text) == text.hash());
}

TEST_CASE("[Hashing] Table keys: only String and CharString use the byte hash") {
	const String text = "table key";
	CHECK(hash_table_key<HashMapHasherDefault>(text) == hash_buffer(text.ptr(), text.length() * sizeof(char32_t)));
	const CharString utf8 = text.utf8();
	CHECK(hash_table_key<HashMapHasherDefault>(utf8) == hash_buffer(utf8.ptr(), utf8.length()));
	CHECK(hash_table_key<HashMapHasherDefault>(String()) == hash_buffer(nullptr, 0));
	CHECK(hash_table_key<HashMapHasherDefault>(uint32_t(42)) == HashMapHasherDefault::hash(uint32_t(42)));
	CHECK(hash_table_key<HashMapHasherDefault>(int64_t(-7)) == HashMapHasherDefault::hash(int64_t(-7)));
	CHECK(hash_table_key<ConstantHasher>(42) == 0);
}

TEST_CASE("[HashMap][AHashMap][HashSet] String keys keep insertion order and lookups") {
	HashMap<String, int> map;
	AHashMap<String, int> amap;
	HashSet<String> set;
	for (int i = 0; i < 600; i++) {
		// Mix short keys and keys above the XXH3 threshold (64 characters = 256 bytes).
		const String key = (i % 3 == 0) ? String("k").repeat(64 + i % 5) + itos(i) : itos(i);
		map.insert(key, i);
		amap.insert(key, i);
		set.insert(key);
	}
	for (int i = 0; i < 600; i += 2) {
		const String key = (i % 3 == 0) ? String("k").repeat(64 + i % 5) + itos(i) : itos(i);
		CHECK(map.erase(key));
		CHECK(amap.erase(key));
		CHECK(set.erase(key));
	}
	CHECK(map.size() == 300);
	CHECK(amap.size() == 300);
	CHECK(set.size() == 300);
	int expected = 1;
	for (const KeyValue<String, int> &E : map) {
		CHECK(E.value == expected);
		expected += 2;
	}
	for (int i = 1; i < 600; i += 2) {
		const String key = (i % 3 == 0) ? String("k").repeat(64 + i % 5) + itos(i) : itos(i);
		CHECK(map[key] == i);
		CHECK(amap[key] == i);
		CHECK(set.has(key));
		CHECK_FALSE(map.has(key + "x"));
	}
}

} // namespace TestHashBuffer
