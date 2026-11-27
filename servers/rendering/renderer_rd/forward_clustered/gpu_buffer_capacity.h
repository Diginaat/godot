/**************************************************************************/
/*  gpu_buffer_capacity.h                                                 */
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

#include "core/math/math_funcs_binary.h"
#include "core/typedefs.h"

// Element capacity for a GPU buffer that grows to hold p_needed elements.
// Small buffers double (cheap in bytes, few reallocations). Large ones get an
// eighth of headroom in steps of 16,384 elements: rounding 262,145 members
// up to 524,288 doubled the GPU path's per-member buffers in a 200k-node
// level (88 MB of instance data for 46 MB in use). See OD1 in
// docs/optimization/DECISIONS.md.
inline uint32_t gpu_buffer_capacity(uint32_t p_needed, uint32_t p_minimum = 1) {
	const uint32_t needed = MAX(p_needed, MAX(p_minimum, 1u));
	if (needed <= 65536u) {
		return Math::next_power_of_2(needed);
	}
	const uint64_t capacity = (uint64_t(needed) + needed / 8 + 16383u) & ~uint64_t(16383u);
	return uint32_t(MIN(capacity, uint64_t(UINT32_MAX)));
}

// Whether a buffer of p_capacity elements can stay for p_needed: big enough,
// and, past 16,384 elements, not more than four times too big. A buffer that
// fails is recreated at gpu_buffer_capacity(), which leaves at least 1/8 of
// headroom, so content that shrinks gives its memory back (a large level
// unloaded) without resizing back and forth. Only for buffers sized by the
// scene's content, not by what one frame sees.
inline bool gpu_buffer_fits(uint32_t p_capacity, uint32_t p_needed, uint32_t p_minimum = 1) {
	const uint32_t needed = MAX(p_needed, MAX(p_minimum, 1u));
	return p_capacity >= needed && (p_capacity <= 16384u || uint64_t(needed) * 4 >= p_capacity);
}
