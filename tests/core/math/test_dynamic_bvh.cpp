/**************************************************************************/
/*  test_dynamic_bvh.cpp                                                  */
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

TEST_FORCE_LINK(test_dynamic_bvh)

#include "core/math/dynamic_bvh.h"
#include "core/math/random_pcg.h"
#include "core/templates/hash_set.h"

namespace TestDynamicBVH {

// Brute force with the tree's own scalar tests: the query of a leaf box must
// give the same answer whatever path the tree takes to it.
static bool volume_intersects(const Vector3 &p_min, const Vector3 &p_max, const Vector3 &p_b_min, const Vector3 &p_b_max) {
	return p_min.x <= p_b_max.x && p_max.x >= p_b_min.x && p_min.y <= p_b_max.y && p_max.y >= p_b_min.y && p_min.z <= p_b_max.z && p_max.z >= p_b_min.z;
}

static bool volume_intersects_convex(const Vector3 &p_min, const Vector3 &p_max, const Plane *p_planes, int p_plane_count, const Vector3 *p_points, int p_point_count) {
	const Vector3 half_extents = (p_max - p_min) * 0.5;
	const Vector3 ofs = p_min + half_extents;
	for (int i = 0; i < p_plane_count; i++) {
		const Plane &p = p_planes[i];
		Vector3 point((p.normal.x > 0) ? -half_extents.x : half_extents.x, (p.normal.y > 0) ? -half_extents.y : half_extents.y, (p.normal.z > 0) ? -half_extents.z : half_extents.z);
		point += ofs;
		if (p.is_point_over(point)) {
			return false;
		}
	}
	for (int k = 0; k < 3; k++) {
		int positive = 0;
		int negative = 0;
		for (int i = 0; i < p_point_count; i++) {
			positive += p_points[i][k] > ofs[k] + half_extents[k] ? 1 : 0;
			negative += p_points[i][k] < ofs[k] - half_extents[k] ? 1 : 0;
		}
		if (positive == p_point_count || negative == p_point_count) {
			return false;
		}
	}
	return true;
}

struct Collector {
	HashSet<uintptr_t> found;
	bool operator()(void *p_data) {
		found.insert(uintptr_t(p_data));
		return false;
	}
};

struct Box {
	AABB aabb;
	DynamicBVH::ID id;
	bool alive = false;
};

static AABB random_box(RandomPCG &p_rng, real_t p_extent, real_t p_max_size) {
	const Vector3 position(p_rng.random(-p_extent, p_extent), p_rng.random(-p_extent, p_extent), p_rng.random(-p_extent, p_extent));
	const Vector3 size(p_rng.random(real_t(0.0), p_max_size), p_rng.random(real_t(0.0), p_max_size), p_rng.random(real_t(0.0), p_max_size));
	return AABB(position, size);
}

static void check_queries(DynamicBVH &p_bvh, const LocalVector<Box> &p_boxes, RandomPCG &p_rng) {
	int checked_hits = 0;
	for (int q = 0; q < 40; q++) {
		// Box query.
		const AABB query = random_box(p_rng, 50.0, 30.0);
		Collector result;
		p_bvh.aabb_query(query, result);
		HashSet<uintptr_t> expected;
		for (uint32_t i = 0; i < p_boxes.size(); i++) {
			const Box &b = p_boxes[i];
			if (b.alive && volume_intersects(b.aabb.position, b.aabb.position + b.aabb.size, query.position, query.position + query.size)) {
				expected.insert(uintptr_t(i + 1));
			}
		}
		CHECK(result.found == expected);
		checked_hits += expected.size();

		// Convex query: the six planes and eight corners of a box (normals out).
		const AABB hull = random_box(p_rng, 40.0, 40.0);
		const Vector3 lo = hull.position;
		const Vector3 hi = hull.position + hull.size;
		const Plane planes[6] = {
			Plane(Vector3(1, 0, 0), hi.x), Plane(Vector3(-1, 0, 0), -lo.x),
			Plane(Vector3(0, 1, 0), hi.y), Plane(Vector3(0, -1, 0), -lo.y),
			Plane(Vector3(0, 0, 1), hi.z), Plane(Vector3(0, 0, -1), -lo.z)
		};
		Vector3 points[8];
		for (int c = 0; c < 8; c++) {
			points[c] = Vector3((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
		}
		Collector convex;
		p_bvh.convex_query(planes, 6, points, 8, convex);
		HashSet<uintptr_t> expected_convex;
		for (uint32_t i = 0; i < p_boxes.size(); i++) {
			const Box &b = p_boxes[i];
			const Vector3 b_min = b.aabb.position;
			const Vector3 b_max = b.aabb.position + b.aabb.size;
			if (b.alive && volume_intersects(b_min, b_max, lo, hi) && volume_intersects_convex(b_min, b_max, planes, 6, points, 8)) {
				expected_convex.insert(uintptr_t(i + 1));
			}
		}
		CHECK(convex.found == expected_convex);
		checked_hits += expected_convex.size();
	}
	CHECK(checked_hits > 0); // The queries hit something.
}

TEST_CASE("[DynamicBVH] Queries against brute force through inserts, updates, removes and optimizing") {
	RandomPCG rng(1234);
	DynamicBVH bvh;
	LocalVector<Box> boxes;
	boxes.resize(600);
	for (uint32_t i = 0; i < boxes.size(); i++) {
		boxes[i].aabb = random_box(rng, 50.0, 6.0);
		boxes[i].id = bvh.insert(boxes[i].aabb, reinterpret_cast<void *>(uintptr_t(i + 1)));
		boxes[i].alive = true;
	}
	check_queries(bvh, boxes, rng);

	for (uint32_t i = 0; i < boxes.size(); i += 2) {
		boxes[i].aabb = random_box(rng, 50.0, 6.0);
		bvh.update(boxes[i].id, boxes[i].aabb);
	}
	for (uint32_t i = 1; i < boxes.size(); i += 4) {
		bvh.remove(boxes[i].id);
		boxes[i].alive = false;
	}
	check_queries(bvh, boxes, rng);

	bvh.optimize_incremental(8);
	check_queries(bvh, boxes, rng);
	bvh.optimize_top_down(16);
	check_queries(bvh, boxes, rng);
	bvh.optimize_bottom_up();
	check_queries(bvh, boxes, rng);

	// Degenerate boxes: points, and many equal boxes (proximity ties).
	for (uint32_t i = 0; i < boxes.size(); i += 3) {
		if (boxes[i].alive) {
			boxes[i].aabb = (i % 2) ? AABB(Vector3(1, 2, 3), Vector3()) : AABB(Vector3(-4, 0, 4), Vector3(1, 1, 1));
			bvh.update(boxes[i].id, boxes[i].aabb);
		}
	}
	check_queries(bvh, boxes, rng);
	bvh.clear();
	CHECK(bvh.is_empty());
}

} // namespace TestDynamicBVH
