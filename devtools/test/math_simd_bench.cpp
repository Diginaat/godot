/**************************************************************************/
/*  math_simd_bench.cpp                                                   */
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

// Exploratory float32/x86 benchmark. No engine math implementation is changed.
// SSE4.2-capable CPU required. Existing Vector3 remains packed (12 bytes).
#include "core/math/vector3.h"

#include <immintrin.h>
#include <intrin.h>
#include <malloc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr int N = 16384;
constexpr int REPEATS = 128;
static_assert(sizeof(Vector3) == 12, "Exploratory benchmark requires float32 Godot layout.");
volatile double sink = 0;

struct SoA {
	float *x;
	float *y;
	float *z;
	SoA() {
		x = static_cast<float *>(_aligned_malloc(N * sizeof(float), 16));
		y = static_cast<float *>(_aligned_malloc(N * sizeof(float), 16));
		z = static_cast<float *>(_aligned_malloc(N * sizeof(float), 16));
		if (!x || !y || !z) {
			std::abort();
		}
	}
	~SoA() {
		_aligned_free(x);
		_aligned_free(y);
		_aligned_free(z);
	}
	SoA(const SoA &) = delete;
	SoA &operator=(const SoA &) = delete;
};

void pack(const Vector3 *p_src, SoA &p_dst) {
	for (int i = 0; i < N; i++) {
		p_dst.x[i] = p_src[i].x;
		p_dst.y[i] = p_src[i].y;
		p_dst.z[i] = p_src[i].z;
	}
}

__m128 lanes(const Vector3 &p_v) {
	// Component reads only: never read a fourth float beyond Vector3.
	return _mm_setr_ps(p_v.x, p_v.y, p_v.z, 0.0f);
}

__declspec(noinline) void dot_scalar(const Vector3 *p_a, const Vector3 *p_b, float *p_out) {
	for (int i = 0; i < N; i++) {
		p_out[i] = p_a[i].dot(p_b[i]);
	}
}
__declspec(noinline) void dot_sse(const Vector3 *p_a, const Vector3 *p_b, float *p_out) {
	for (int i = 0; i < N; i++) {
		p_out[i] = _mm_cvtss_f32(_mm_dp_ps(lanes(p_a[i]), lanes(p_b[i]), 0x71));
	}
}
__declspec(noinline) void dot_soa_scalar(const SoA &p_a, const SoA &p_b, float *p_out) {
	// Compiler auto-vectorization stays enabled: do not handicap baseline.
	for (int i = 0; i < N; i++) {
		p_out[i] = p_a.x[i] * p_b.x[i] + p_a.y[i] * p_b.y[i] + p_a.z[i] * p_b.z[i];
	}
}
__declspec(noinline) void dot_soa_sse(const SoA &p_a, const SoA &p_b, float *p_out) {
	for (int i = 0; i < N; i += 4) {
		const __m128 x = _mm_mul_ps(_mm_load_ps(p_a.x + i), _mm_load_ps(p_b.x + i));
		const __m128 y = _mm_mul_ps(_mm_load_ps(p_a.y + i), _mm_load_ps(p_b.y + i));
		const __m128 z = _mm_mul_ps(_mm_load_ps(p_a.z + i), _mm_load_ps(p_b.z + i));
		_mm_store_ps(p_out + i, _mm_add_ps(_mm_add_ps(x, y), z));
	}
}
__declspec(noinline) void cross_scalar(const Vector3 *p_a, const Vector3 *p_b, Vector3 *p_out) {
	for (int i = 0; i < N; i++) {
		p_out[i] = p_a[i].cross(p_b[i]);
	}
}
__declspec(noinline) void cross_sse(const Vector3 *p_a, const Vector3 *p_b, Vector3 *p_out) {
	for (int i = 0; i < N; i++) {
		const __m128 a = lanes(p_a[i]);
		const __m128 b = lanes(p_b[i]);
		const __m128 ayzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
		const __m128 azxy = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
		const __m128 byzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
		const __m128 bzxy = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
		const __m128 result = _mm_sub_ps(_mm_mul_ps(ayzx, bzxy), _mm_mul_ps(azxy, byzx));
		p_out[i].x = _mm_cvtss_f32(result);
		p_out[i].y = _mm_cvtss_f32(_mm_shuffle_ps(result, result, _MM_SHUFFLE(1, 1, 1, 1)));
		p_out[i].z = _mm_cvtss_f32(_mm_shuffle_ps(result, result, _MM_SHUFFLE(2, 2, 2, 2)));
	}
}

template <typename Operation>
void measure(const char *p_name, int p_sample, Operation p_op) {
	const auto start = std::chrono::steady_clock::now();
	for (int repeat = 0; repeat < REPEATS; repeat++) {
		std::atomic_signal_fence(std::memory_order_seq_cst);
		p_op();
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (N * REPEATS);
	if (p_sample >= 0) {
		std::printf("%s,%d,%.9f\n", p_name, p_sample, ns);
	}
}

void equal(float p_a, float p_b) {
	if (!std::isfinite(p_a) || !std::isfinite(p_b) || std::abs(p_a - p_b) > 1e-5f * std::max(1.0f, std::abs(p_a))) {
		std::fprintf(stderr, "FAIL: numerical mismatch %.9g != %.9g\n", p_a, p_b);
		std::exit(2);
	}
}
} // namespace

int main() {
	int cpu[4];
	__cpuid(cpu, 1);
	if (!(cpu[2] & (1 << 20))) {
		std::fprintf(stderr, "SSE4.2 required\n");
		return 2;
	}
	std::vector<Vector3> a(N), b(N), cross_out(N);
	std::vector<float> reference(N);
	SoA sa, sb;
	float *out = static_cast<float *>(_aligned_malloc(N * sizeof(float), 16));
	if (!out) {
		return 2;
	}
	uint32_t state = 0x12345;
	auto value = [&]() {
		state = state * 1664525U + 1013904223U;
		return (int(state >> 16) - 32768) / 4096.0f;
	};
	for (int i = 0; i < N; i++) {
		a[i] = Vector3(value(), value(), value());
		b[i] = Vector3(value(), value(), value());
	}
	pack(a.data(), sa);
	pack(b.data(), sb);
	dot_scalar(a.data(), b.data(), reference.data());
	dot_sse(a.data(), b.data(), out);
	for (int i = 0; i < N; i++) {
		equal(reference[i], out[i]);
	}
	dot_soa_sse(sa, sb, out);
	for (int i = 0; i < N; i++) {
		equal(reference[i], out[i]);
	}
	cross_sse(a.data(), b.data(), cross_out.data());
	for (int i = 0; i < N; i++) {
		const Vector3 expected = a[i].cross(b[i]);
		equal(expected.x, cross_out[i].x);
		equal(expected.y, cross_out[i].y);
		equal(expected.z, cross_out[i].z);
	}
	std::puts("case,sample,ns_per_vector");
	for (int sample = -1; sample < 11; sample++) {
		// Reverse case ordering every sample to counter systematic timing drift.
		for (int slot = 0; slot < 7; slot++) {
			const int which = sample % 2 ? slot : 6 - slot;
			switch (which) {
				case 0:
					measure("dot_aos_godot", sample, [&]() { dot_scalar(a.data(), b.data(), out); });
					break;
				case 1:
					measure("dot_aos_sse42", sample, [&]() { dot_sse(a.data(), b.data(), out); });
					break;
				case 2:
					measure("dot_soa_compiler", sample, [&]() { dot_soa_scalar(sa, sb, out); });
					break;
				case 3:
					measure("dot_soa_sse42", sample, [&]() { dot_soa_sse(sa, sb, out); });
					break;
				case 4:
					measure("dot_pack_and_sse42", sample, [&]() { pack(a.data(), sa); pack(b.data(), sb); dot_soa_sse(sa, sb, out); });
					break;
				case 5:
					measure("cross_aos_godot", sample, [&]() { cross_scalar(a.data(), b.data(), cross_out.data()); });
					break;
				case 6:
					measure("cross_aos_sse42", sample, [&]() { cross_sse(a.data(), b.data(), cross_out.data()); });
					break;
			}
			double checksum = 0;
			if (which < 5) {
				for (int i = 0; i < N; i++) {
					checksum += out[i];
				}
			} else {
				for (int i = 0; i < N; i++) {
					checksum += cross_out[i].x + cross_out[i].y + cross_out[i].z;
				}
			}
			sink = checksum;
		}
	}
	_aligned_free(out);
	return 0;
}
