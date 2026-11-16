/**************************************************************************/
/*  test_node_3d.cpp                                                      */
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

TEST_FORCE_LINK(test_node_3d)

#include "core/math/random_pcg.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestNode3D {

class NotifiedNode3D : public Node3D {
	GDCLASS(NotifiedNode3D, Node3D);

protected:
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_TRANSFORM_CHANGED) {
			notifications++;
			if (read_in_handler) {
				_ALLOW_DISCARD_ get_global_transform();
			}
		}
	}

public:
	int notifications = 0;
	// Engine nodes read their global transform when notified; a script that
	// enabled notifications may not.
	bool read_in_handler = false;

	void set_ignore(bool p_ignore) { set_ignore_transform_notification(p_ignore); }
};

// A model of transform notifications as propagation without any early-out
// defines them: moving a node queues every node of its subtree (down to
// top-level nodes) that wants notifications at that moment, entering the tree
// queues it, and a flush notifies each queued node once
// (docs/implementation/scene-runtime-roadmap.md, M4).
struct Model {
	LocalVector<NotifiedNode3D *> nodes;
	HashMap<NotifiedNode3D *, bool> notify;
	HashMap<NotifiedNode3D *, bool> ignore;
	HashSet<NotifiedNode3D *> queued;

	bool wants(NotifiedNode3D *p_node) const { return notify[p_node] && !ignore[p_node]; }

	void moved(NotifiedNode3D *p_node, bool p_origin = true) {
		if (!p_origin && p_node->is_set_as_top_level()) {
			return;
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			moved(Object::cast_to<NotifiedNode3D>(p_node->get_child(i)), false);
		}
		if (wants(p_node)) {
			queued.insert(p_node);
		}
	}

	void entered(NotifiedNode3D *p_node) {
		if (wants(p_node)) {
			queued.insert(p_node);
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			entered(Object::cast_to<NotifiedNode3D>(p_node->get_child(i)));
		}
	}
};

static Transform3D reference_global(Node3D *p_node) {
	Node3D *parent = p_node->get_parent_node_3d();
	if (!parent || p_node->is_set_as_top_level()) {
		return p_node->get_transform();
	}
	return reference_global(parent) * p_node->get_transform();
}

static bool is_ancestor_or_self(Node *p_ancestor, Node *p_node) {
	return p_ancestor == p_node || p_ancestor->is_ancestor_of(p_node);
}

static void run_random_operations(bool p_with_physics_interpolation) {
	SceneTree *tree = SceneTree::get_singleton();
	Node3D *root = memnew(Node3D);
	tree->get_root()->add_child(root);

	RandomPCG rng(12345);
	Model model;
	for (int i = 0; i < 80; i++) {
		NotifiedNode3D *n = memnew(NotifiedNode3D);
		n->set_name(vformat("N%d", i));
		n->read_in_handler = rng.rand(2) == 0;
		const bool notify = rng.rand(3) != 0;
		n->set_notify_transform(notify);
		model.notify[n] = notify;
		model.ignore[n] = false;
		// Random trees with chains (parents among the last few) and fans.
		Node *parent = model.nodes.is_empty() || rng.rand(8) == 0 ? (Node *)root : (Node *)model.nodes[model.nodes.size() - 1 - rng.rand(MIN(4u, model.nodes.size()))];
		parent->add_child(n);
		n->set_position(Vector3(rng.randf(), rng.randf(), rng.randf()));
		model.nodes.push_back(n);
	}
	tree->flush_transform_notifications();
	for (NotifiedNode3D *n : model.nodes) {
		n->notifications = 0;
	}

	int flushes = 0;
	int notified = 0;
	for (int step = 0; step < 4000; step++) {
		NotifiedNode3D *n = model.nodes[rng.rand(model.nodes.size())];
		switch (rng.rand(11)) {
			case 0:
			case 1:
			case 2: { // Move.
				n->set_position(Vector3(rng.randf(), rng.randf(), rng.randf()) * 4.0);
				model.moved(n);
			} break;
			case 3: { // Rotate (a second propagation through the same subtree).
				n->rotate_y(rng.randf());
				model.moved(n);
			} break;
			case 4:
			case 5: { // Read, which cleans the node and its parents.
				CHECK(n->get_global_transform().is_equal_approx(reference_global(n)));
			} break;
			case 6: { // Notifications on or off.
				const bool notify = rng.rand(2) == 0;
				n->set_notify_transform(notify);
				model.notify[n] = notify;
			} break;
			case 7: { // Ignore notifications, move, stop ignoring (as physics bodies do).
				n->set_ignore(true);
				model.ignore[n] = true;
				n->set_position(Vector3(rng.randf(), rng.randf(), rng.randf()));
				model.moved(n);
				if (rng.rand(2) == 0) {
					n->set_ignore(false);
					model.ignore[n] = false;
				}
			} break;
			case 8: { // Top level on or off (sets the transform first).
				n->set_as_top_level(!n->is_set_as_top_level());
				model.moved(n);
			} break;
			case 9: { // Move under another node of the tree (exit and enter).
				NotifiedNode3D *to = model.nodes[rng.rand(model.nodes.size())];
				if (is_ancestor_or_self(n, to)) {
					break;
				}
				// Leaving the tree drops the subtree's queued entries.
				for (NotifiedNode3D *m : model.nodes) {
					if (is_ancestor_or_self(n, m)) {
						model.queued.erase(m);
					}
				}
				n->get_parent()->remove_child(n);
				to->add_child(n);
				model.entered(n);
			} break;
			case 10: { // Flush: each queued node notified once, the others not.
				if (p_with_physics_interpolation && rng.rand(4) == 0) {
					tree->set_physics_interpolation_enabled(!tree->is_physics_interpolation_enabled());
				}
				tree->flush_transform_notifications();
				flushes++;
				for (NotifiedNode3D *m : model.nodes) {
					const int expected = model.queued.has(m) ? 1 : 0;
					if (m->notifications != expected) {
						FAIL_CHECK(vformat("step %d: %s notified %d times, expected %d", step, m->get_name(), m->notifications, expected));
					}
					notified += m->notifications;
					m->notifications = 0;
				}
				model.queued.clear();
			} break;
		}
	}
	for (NotifiedNode3D *n : model.nodes) {
		CHECK(n->get_global_transform().is_equal_approx(reference_global(n)));
	}
	CHECK(flushes > 100);
	CHECK(notified > 100);

	if (tree->is_physics_interpolation_enabled()) {
		tree->set_physics_interpolation_enabled(false);
	}
	memdelete(root); // Frees the nodes.
}

TEST_CASE("[SceneTree][Node3D] Transform notifications and global transforms through random changes") {
	GDREGISTER_CLASS(NotifiedNode3D);
	SUBCASE("Without physics interpolation") {
		run_random_operations(false);
	}
	SUBCASE("Physics interpolation toggled") {
		run_random_operations(true);
	}
}

TEST_CASE("[SceneTree][Node3D] A dirty child that starts to want notifications is notified by its parent's next move") {
	SceneTree *tree = SceneTree::get_singleton();
	NotifiedNode3D *parent = memnew(NotifiedNode3D);
	NotifiedNode3D *child = memnew(NotifiedNode3D);
	parent->add_child(child);
	tree->get_root()->add_child(parent);
	tree->flush_transform_notifications();

	// Dirty, notifications off: not queued.
	parent->set_position(Vector3(1, 0, 0));
	child->set_notify_transform(true);
	tree->flush_transform_notifications();
	CHECK_EQ(child->notifications, 0);

	// The child stays dirty (nothing read it); the parent moves again.
	parent->set_position(Vector3(2, 0, 0));
	tree->flush_transform_notifications();
	CHECK_EQ(child->notifications, 1);
	CHECK(child->get_global_transform().origin.is_equal_approx(Vector3(2, 0, 0)));

	// Notified without reading its transform, then the parent moves twice.
	child->notifications = 0;
	parent->set_position(Vector3(3, 0, 0));
	tree->flush_transform_notifications();
	parent->set_position(Vector3(4, 0, 0));
	parent->set_position(Vector3(5, 0, 0));
	tree->flush_transform_notifications();
	CHECK_EQ(child->notifications, 2);

	memdelete(parent);
}

} // namespace TestNode3D
