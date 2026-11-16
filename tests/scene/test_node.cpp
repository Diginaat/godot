/**************************************************************************/
/*  test_node.cpp                                                         */
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

TEST_FORCE_LINK(test_node)

#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_utils.h"

namespace TestNode {

class TestNode : public Node {
	GDCLASS(TestNode, Node);

protected:
	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_INTERNAL_PROCESS: {
				internal_process_counter++;
				push_self();
			} break;
			case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
				internal_physics_process_counter++;
				push_self();
			} break;
			case NOTIFICATION_PROCESS: {
				process_counter++;
				push_self();
			} break;
			case NOTIFICATION_PHYSICS_PROCESS: {
				physics_process_counter++;
				push_self();
			} break;
		}
	}

	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_exported_node", "node"), &TestNode::set_exported_node);
		ClassDB::bind_method(D_METHOD("get_exported_node"), &TestNode::get_exported_node);
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "exported_node", PROPERTY_HINT_NODE_TYPE, "Node"), "set_exported_node", "get_exported_node");

		ClassDB::bind_method(D_METHOD("set_exported_nodes", "node"), &TestNode::set_exported_nodes);
		ClassDB::bind_method(D_METHOD("get_exported_nodes"), &TestNode::get_exported_nodes);
		ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "exported_nodes", PROPERTY_HINT_TYPE_STRING, "24/34:Node"), "set_exported_nodes", "get_exported_nodes");
	}

private:
	void push_self() {
		if (callback_list) {
			callback_list->push_back(this);
		}
	}

public:
	int internal_process_counter = 0;
	int internal_physics_process_counter = 0;
	int process_counter = 0;
	int physics_process_counter = 0;

	Node *exported_node = nullptr;
	Array exported_nodes;

	List<Node *> *callback_list = nullptr;

	void set_exported_node(Node *p_node) { exported_node = p_node; }
	Node *get_exported_node() const { return exported_node; }

	void set_exported_nodes(const Array &p_nodes) { exported_nodes = p_nodes; }
	Array get_exported_nodes() const { return exported_nodes; }

	TestNode() {
		Node *internal = memnew(Node);
		add_child(internal, false, INTERNAL_MODE_FRONT);
		internal = memnew(Node);
		add_child(internal, false, INTERNAL_MODE_BACK);
	}
};

TEST_CASE("[SceneTree][Node] Testing node operations with a very simple scene tree") {
	Node *node = memnew(Node);

	// Check initial scene tree setup.
	CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 0);
	CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 1);

	// Check initial node setup.
	CHECK(node->get_name() == StringName());
	CHECK_FALSE(node->is_inside_tree());
	CHECK_EQ(node->get_parent(), nullptr);
	ERR_PRINT_OFF;
	CHECK(node->get_path().is_empty());
	ERR_PRINT_ON;
	CHECK_EQ(node->get_child_count(), 0);

	SceneTree::get_singleton()->get_root()->add_child(node);

	CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 1);
	CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 2);

	CHECK(node->get_name() != StringName());
	CHECK(node->is_inside_tree());
	CHECK_EQ(SceneTree::get_singleton()->get_root(), node->get_parent());
	CHECK_FALSE(node->get_path().is_empty());
	CHECK_EQ(node->get_child_count(), 0);

	SUBCASE("Node should be accessible as first child") {
		Node *child = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child, node);
	}

	SUBCASE("Node should be accessible via the node path") {
		Node *child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(node->get_path());
		CHECK_EQ(child_by_path, node);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("Node"));
		CHECK_EQ(child_by_path, nullptr);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("/root/Node"));
		CHECK_EQ(child_by_path, nullptr);

		node->set_name("Node");

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(node->get_path());
		CHECK_EQ(child_by_path, node);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("Node"));
		CHECK_EQ(child_by_path, node);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("/root/Node"));
		CHECK_EQ(child_by_path, node);
	}

	SUBCASE("Node should be accessible via group") {
		Vector<Node *> nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK(nodes.is_empty());

		node->add_to_group("nodes");

		nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK_EQ(nodes.size(), 1);
		Node *E = nodes.get(0);
		CHECK_EQ(E, node);
	}

	SUBCASE("Node should be possible to find") {
		Node *child = SceneTree::get_singleton()->get_root()->find_child("Node", true, false);
		CHECK_EQ(child, nullptr);

		node->set_name("Node");

		child = SceneTree::get_singleton()->get_root()->find_child("Node", true, false);
		CHECK_EQ(child, node);
	}

	SUBCASE("Node should be possible to remove") {
		SceneTree::get_singleton()->get_root()->remove_child(node);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 0);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 1);

		CHECK_FALSE(node->is_inside_tree());
		CHECK_EQ(node->get_parent(), nullptr);
		ERR_PRINT_OFF;
		CHECK(node->get_path().is_empty());
		ERR_PRINT_ON;
	}

	SUBCASE("Node should be possible to move") {
		SceneTree::get_singleton()->get_root()->move_child(node, 0);

		Node *child = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child, node);
		CHECK(node->is_inside_tree());
	}

	SUBCASE("Node should be possible to reparent") {
		node->reparent(SceneTree::get_singleton()->get_root());

		Node *child = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child, node);
		CHECK(node->is_inside_tree());
	}

	SUBCASE("Node should be possible to duplicate") {
		node->set_name("MyName");

		Node *duplicate = node->duplicate();

		CHECK_FALSE(node == duplicate);
		CHECK_FALSE(duplicate->is_inside_tree());
		CHECK_EQ(duplicate->get_name(), node->get_name());

		memdelete(duplicate);
	}

	memdelete(node);
}

TEST_CASE("[SceneTree][Node] Testing node operations with a more complex simple scene tree") {
	Node *node1 = memnew(Node);
	Node *node2 = memnew(Node);
	Node *node1_1 = memnew(Node);
	Node *root = SceneTree::get_singleton()->get_root();

	root->add_child(node1);
	root->add_child(node2);

	node1->add_child(node1_1);

	CHECK(node1_1->is_inside_tree());
	CHECK_EQ(node1_1->get_parent(), node1);
	CHECK_EQ(node1->get_child_count(), 1);

	CHECK_EQ(root->is_ancestor_of(node1), true);
	CHECK_EQ(root->is_ancestor_of(node2), true);
	CHECK_EQ(root->is_ancestor_of(node1_1), true);
	CHECK_EQ(node1->is_ancestor_of(node1), false);
	CHECK_EQ(node1->is_ancestor_of(node1_1), true);
	CHECK_EQ(node2->is_ancestor_of(node1_1), false);
	CHECK_EQ(node2->is_ancestor_of(node1), false);

	CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 2);
	CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 4);

	SUBCASE("Nodes should be accessible via get_child(..)") {
		Node *child1 = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child1, node1);

		Node *child2 = SceneTree::get_singleton()->get_root()->get_child(1);
		CHECK_EQ(child2, node2);

		Node *child1_1 = node1->get_child(0);
		CHECK_EQ(child1_1, node1_1);
	}

	SUBCASE("Removed nodes should also remove their children from the scene tree") {
		// Should also remove node1_1 from the scene tree.
		SceneTree::get_singleton()->get_root()->remove_child(node1);

		CHECK_EQ(node1->get_child_count(), 1);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 1);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 2);

		// First child should now be the second node.
		Node *child1 = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child1, node2);
	}

	SUBCASE("Removed children nodes should not affect their parent in the scene tree") {
		node1->remove_child(node1_1);

		CHECK_EQ(node1_1->get_parent(), nullptr);
		CHECK_EQ(node1->get_child_count(), 0);

		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 3);
	}

	SUBCASE("Nodes should be in the expected order when a node is moved to the back") {
		SceneTree::get_singleton()->get_root()->move_child(node1, 1);

		Node *child1 = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child1, node2);

		Node *child2 = SceneTree::get_singleton()->get_root()->get_child(1);
		CHECK_EQ(child2, node1);
	}

	SUBCASE("Nodes should be in the expected order when a node is moved to the front") {
		SceneTree::get_singleton()->get_root()->move_child(node2, 0);

		Node *child1 = SceneTree::get_singleton()->get_root()->get_child(0);
		CHECK_EQ(child1, node2);

		Node *child2 = SceneTree::get_singleton()->get_root()->get_child(1);
		CHECK_EQ(child2, node1);
	}

	SUBCASE("Nodes should be in the expected order when reparented (remove/add)") {
		CHECK_EQ(node2->get_child_count(), 0);

		node1->remove_child(node1_1);
		CHECK_EQ(node1->get_child_count(), 0);
		CHECK_EQ(node1_1->get_parent(), nullptr);

		node2->add_child(node1_1);
		CHECK_EQ(node2->get_child_count(), 1);
		CHECK_EQ(node1_1->get_parent(), node2);

		Node *child = node2->get_child(0);
		CHECK_EQ(child, node1_1);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 2);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 4);
	}

	SUBCASE("Nodes should be in the expected order when reparented") {
		CHECK_EQ(node2->get_child_count(), 0);

		node1_1->reparent(node2);

		CHECK_EQ(node1->get_child_count(), 0);
		CHECK_EQ(node2->get_child_count(), 1);
		CHECK_EQ(node1_1->get_parent(), node2);

		Node *child = node2->get_child(0);
		CHECK_EQ(child, node1_1);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 2);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 4);
	}

	SUBCASE("Nodes should be possible to find") {
		Node *child = SceneTree::get_singleton()->get_root()->find_child("NestedNode", true, false);
		CHECK_EQ(child, nullptr);

		TypedArray<Node> children = SceneTree::get_singleton()->get_root()->find_children("NestedNode", "", true, false);
		CHECK_EQ(children.size(), 0);

		node1->set_name("Node1");
		node2->set_name("Node2");
		node1_1->set_name("NestedNode");

		child = SceneTree::get_singleton()->get_root()->find_child("NestedNode", true, false);
		CHECK_EQ(child, node1_1);

		children = SceneTree::get_singleton()->get_root()->find_children("NestedNode", "", true, false);
		CHECK_EQ(children.size(), 1);
		CHECK_EQ(Object::cast_to<Node>(children[0]), node1_1);

		// First node that matches with the name is node1.
		child = SceneTree::get_singleton()->get_root()->find_child("Node?", true, false);
		CHECK_EQ(child, node1);

		children = SceneTree::get_singleton()->get_root()->find_children("Node?", "", true, false);
		CHECK_EQ(children.size(), 2);
		CHECK_EQ(Object::cast_to<Node>(children[0]), node1);
		CHECK_EQ(Object::cast_to<Node>(children[1]), node2);

		SceneTree::get_singleton()->get_root()->move_child(node2, 0);

		// It should be node2, as it is now the first one in the tree.
		child = SceneTree::get_singleton()->get_root()->find_child("Node?", true, false);
		CHECK_EQ(child, node2);

		children = SceneTree::get_singleton()->get_root()->find_children("Node?", "", true, false);
		CHECK_EQ(children.size(), 2);
		CHECK_EQ(Object::cast_to<Node>(children[0]), node2);
		CHECK_EQ(Object::cast_to<Node>(children[1]), node1);
	}

	SUBCASE("Nodes should be accessible via their node path") {
		Node *child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(node1->get_path());
		CHECK_EQ(child_by_path, node1);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(node2->get_path());
		CHECK_EQ(child_by_path, node2);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(node1_1->get_path());
		CHECK_EQ(child_by_path, node1_1);

		node1->set_name("Node1");
		node1_1->set_name("NestedNode");

		child_by_path = node1->get_node_or_null(NodePath("NestedNode"));
		CHECK_EQ(child_by_path, node1_1);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("/root/Node1/NestedNode"));
		CHECK_EQ(child_by_path, node1_1);

		child_by_path = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath("Node1/NestedNode"));
		CHECK_EQ(child_by_path, node1_1);
	}

	SUBCASE("Nodes should be accessible via their groups") {
		Vector<Node *> nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK(nodes.is_empty());

		nodes = SceneTree::get_singleton()->get_nodes_in_group("other_nodes");
		CHECK(nodes.is_empty());

		node1->add_to_group("nodes");
		node2->add_to_group("other_nodes");
		node1_1->add_to_group("nodes");
		node1_1->add_to_group("other_nodes");

		nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK_EQ(nodes.size(), 2);

		Node *E = nodes.get(0);
		CHECK_EQ(E, node1);
		E = nodes.get(1);
		CHECK_EQ(E, node1_1);

		// Clear and try again with the other group.
		nodes.clear();

		nodes = SceneTree::get_singleton()->get_nodes_in_group("other_nodes");
		CHECK_EQ(nodes.size(), 2);

		E = nodes.get(0);
		CHECK_EQ(E, node1_1);
		E = nodes.get(1);
		CHECK_EQ(E, node2);

		// Clear and try again with the other group and one node removed.
		nodes.clear();

		node1->remove_from_group("nodes");
		nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK_EQ(nodes.size(), 1);

		E = nodes.get(0);
		CHECK_EQ(E, node1_1);
	}

	SUBCASE("Nodes added as siblings of another node should be right next to it") {
		node1->remove_child(node1_1);

		node1->add_sibling(node1_1);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 3);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 4);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child(0), node1);
		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child(1), node1_1);
		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child(2), node2);
	}

	SUBCASE("Replaced nodes should be be removed and the replacing node added") {
		SceneTree::get_singleton()->get_root()->remove_child(node2);

		node1->replace_by(node2);

		CHECK_EQ(SceneTree::get_singleton()->get_root()->get_child_count(), 1);
		CHECK_EQ(SceneTree::get_singleton()->get_node_count(), 3);

		CHECK_FALSE(node1->is_inside_tree());
		CHECK(node2->is_inside_tree());

		CHECK_EQ(node1->get_parent(), nullptr);
		CHECK_EQ(node2->get_parent(), SceneTree::get_singleton()->get_root());
		CHECK_EQ(node2->get_child_count(), 1);
		CHECK_EQ(node2->get_child(0), node1_1);
	}

	SUBCASE("Replacing nodes should keep the groups of the replaced nodes") {
		SceneTree::get_singleton()->get_root()->remove_child(node2);

		node1->add_to_group("nodes");
		node1->replace_by(node2, true);

		Vector<Node *> nodes = SceneTree::get_singleton()->get_nodes_in_group("nodes");
		CHECK_EQ(nodes.size(), 1);

		Node *E = nodes.get(0);
		CHECK_EQ(E, node2);
	}

	SUBCASE("Duplicating a node should also duplicate the children") {
		node1->set_name("MyName1");
		node1_1->set_name("MyName1_1");
		Node *duplicate1 = node1->duplicate();

		CHECK_EQ(duplicate1->get_child_count(), node1->get_child_count());
		Node *duplicate1_1 = duplicate1->get_child(0);

		CHECK_EQ(duplicate1_1->get_child_count(), node1_1->get_child_count());

		CHECK_EQ(duplicate1->get_name(), node1->get_name());
		CHECK_EQ(duplicate1_1->get_name(), node1_1->get_name());

		CHECK_FALSE(duplicate1->is_inside_tree());
		CHECK_FALSE(duplicate1_1->is_inside_tree());

		memdelete(duplicate1_1);
		memdelete(duplicate1);
	}

	memdelete(node1_1);
	memdelete(node1);
	memdelete(node2);
}

// Group membership through repeated adds, removals and re-entering the tree
// (each member exactly once, in tree order). docs/implementation/scene-runtime-roadmap.md, M1.
TEST_CASE("[SceneTree][Node] A node's groups keep their order through removals and re-entering the tree") {
	Node *node = memnew(Node);
	const char *names[] = { "g0", "g1", "g2", "g3", "g4" };
	for (const char *name : names) {
		node->add_to_group(name, String(name) == "g1" || String(name) == "g3");
	}
	node->add_to_group("g2"); // Already in it: no change.

	const auto group_names = [](Node *p_node) {
		List<Node::GroupInfo> groups;
		p_node->get_groups(&groups);
		String joined;
		for (const Node::GroupInfo &gi : groups) {
			joined += String(gi.name) + (gi.persistent ? "*" : "") + " ";
		}
		return joined;
	};
	CHECK_EQ(group_names(node), "g0 g1* g2 g3* g4 ");
	CHECK_EQ(node->get_persistent_group_count(), 2);

	// Out of the tree, then in it: the tree finds the node in every group.
	node->remove_from_group("g2");
	CHECK_FALSE(node->is_in_group("g2"));
	CHECK(node->is_in_group("g3"));
	SceneTree::get_singleton()->get_root()->add_child(node);
	for (const char *name : { "g0", "g1", "g3", "g4" }) {
		CHECK_EQ(SceneTree::get_singleton()->get_first_node_in_group(name), node);
	}
	CHECK_FALSE(SceneTree::get_singleton()->has_group("g2"));

	// Inside the tree: removals of the first and last, a re-addition at the end.
	node->remove_from_group("g0");
	node->remove_from_group("g4");
	node->add_to_group("g0");
	CHECK_EQ(group_names(node), "g1* g3* g0 ");
	CHECK_FALSE(SceneTree::get_singleton()->has_group("g4"));
	CHECK_EQ(SceneTree::get_singleton()->get_node_count_in_group("g0"), 1);

	SceneTree::get_singleton()->get_root()->remove_child(node); // Leaves every group.
	for (const char *name : names) {
		CHECK_FALSE(SceneTree::get_singleton()->has_group(name));
	}
	CHECK_EQ(group_names(node), "g1* g3* g0 "); // The node keeps its own.
	memdelete(node);
	for (const char *name : names) {
		CHECK_FALSE(SceneTree::get_singleton()->has_group(name));
	}
}

TEST_CASE("[SceneTree][Node] Group membership through add, remove and re-entering the tree") {
	Node *parent = memnew(Node);
	SceneTree::get_singleton()->get_root()->add_child(parent);
	LocalVector<Node *> children;
	for (int i = 0; i < 64; i++) {
		Node *child = memnew(Node);
		if (i % 2 == 0) {
			child->add_to_group("bench_group"); // Before entering the tree.
		}
		parent->add_child(child);
		if (i % 2 == 1) {
			child->add_to_group("bench_group"); // Inside the tree.
		}
		child->add_to_group("bench_group"); // Already a member: no effect.
		children.push_back(child);
	}
	SceneTree *tree = SceneTree::get_singleton();
	CHECK_EQ(tree->get_node_count_in_group("bench_group"), 64);

	// Out of the tree and back: each node's groups are registered again.
	for (int i = 0; i < 64; i += 3) {
		parent->remove_child(children[i]);
	}
	CHECK_EQ(tree->get_node_count_in_group("bench_group"), 64 - 22);
	for (int i = 0; i < 64; i += 3) {
		parent->add_child(children[i]);
	}
	for (int i = 1; i < 64; i += 4) {
		children[i]->remove_from_group("bench_group");
	}
	// The count before any reader compacts the removed members away (M3).
	CHECK_EQ(tree->get_node_count_in_group("bench_group"), 64 - 16);
	Vector<Node *> members = tree->get_nodes_in_group("bench_group");
	CHECK_EQ(members.size(), 64 - 16);
	HashSet<Node *> unique;
	for (int i = 0; i < members.size(); i++) {
		unique.insert(members[i]);
		CHECK(members[i]->is_in_group("bench_group"));
		if (i > 0) {
			CHECK(members[i]->is_greater_than(members[i - 1])); // Tree order.
		}
	}
	CHECK_EQ(unique.size(), uint32_t(members.size()));

	// The first member removed: the next first in tree order, then removals
	// and re-additions in a row, read once.
	Node *first = tree->get_first_node_in_group("bench_group");
	CHECK_EQ(first, members[0]);
	first->remove_from_group("bench_group");
	CHECK_EQ(tree->get_first_node_in_group("bench_group"), members[1]);
	members[2]->remove_from_group("bench_group");
	members[3]->remove_from_group("bench_group");
	members[2]->add_to_group("bench_group");
	members[5]->remove_from_group("bench_group");
	Vector<Node *> after = tree->get_nodes_in_group("bench_group");
	CHECK_EQ(after.size(), members.size() - 3); // Four removed, one added back.
	for (int i = 1; i < after.size(); i++) {
		CHECK(after[i]->is_greater_than(after[i - 1]));
	}
	CHECK(after.has(members[2]));
	CHECK_FALSE(after.has(members[3]));
	CHECK_FALSE(after.has(members[5]));

	// Many removals after a sort (stale positions: searched removals, then one
	// rewrite of all positions after 128 of them), then more, then a read.
	{
		Node *crowd = memnew(Node);
		parent->add_child(crowd);
		LocalVector<Node *> many;
		for (int i = 0; i < 300; i++) {
			Node *n = memnew(Node);
			crowd->add_child(n);
			n->add_to_group("crowd_group");
			many.push_back(n);
		}
		CHECK_EQ(tree->get_nodes_in_group("crowd_group").size(), 300); // Sorted: positions stale.
		for (int i = 0; i < 200; i++) {
			many[(i * 7) % 300]->remove_from_group("crowd_group"); // 7 and 300 coprime: 200 distinct.
		}
		CHECK_EQ(tree->get_node_count_in_group("crowd_group"), 100);
		Vector<Node *> rest = tree->get_nodes_in_group("crowd_group");
		REQUIRE_EQ(rest.size(), 100);
		for (int i = 1; i < rest.size(); i++) {
			CHECK(rest[i]->is_greater_than(rest[i - 1]));
		}
		for (Node *n : rest) {
			n->remove_from_group("crowd_group");
		}
		CHECK_FALSE(tree->has_group("crowd_group"));
	}

	// Removed while the group is being called: the call goes on with its copy.
	tree->call_group("bench_group", "remove_from_group", "bench_group");
	CHECK_EQ(tree->get_node_count_in_group("bench_group"), 0);
	CHECK_FALSE(tree->has_group("bench_group"));

	memdelete(parent); // Frees the children.
	CHECK_EQ(tree->get_node_count_in_group("bench_group"), 0);
}

// Child order and indices through removals anywhere (holes in the children
// cache), additions, moves and internal children, against an expected list
// after every step. docs/implementation/scene-runtime-roadmap.md, M2.
TEST_CASE("[SceneTree][Node] Child order and indices through removals, additions and moves") {
	Node *parent = memnew(Node);
	SceneTree::get_singleton()->get_root()->add_child(parent);
	LocalVector<Node *> front;
	LocalVector<Node *> external;
	LocalVector<Node *> back;
	auto check = [&]() {
		const int total = int(front.size() + external.size() + back.size());
		REQUIRE_EQ(parent->get_child_count(true), total);
		REQUIRE_EQ(parent->get_child_count(false), int(external.size()));
		int i = 0;
		for (Node *child : front) {
			CHECK_EQ(parent->get_child(i, true), child);
			CHECK_EQ(child->get_index(true), i);
			i++;
		}
		for (uint32_t e = 0; e < external.size(); e++) {
			CHECK_EQ(parent->get_child(i, true), external[e]);
			CHECK_EQ(parent->get_child(int(e), false), external[e]);
			CHECK_EQ(external[e]->get_index(false), int(e));
			CHECK_EQ(external[e]->get_index(true), i);
			i++;
		}
		for (Node *child : back) {
			CHECK_EQ(parent->get_child(i, true), child);
			CHECK_EQ(child->get_index(true), i);
			i++;
		}
	};
	auto add = [&](Node::InternalMode p_mode) {
		Node *child = memnew(Node);
		parent->add_child(child, false, p_mode);
		(p_mode == Node::INTERNAL_MODE_FRONT ? front : (p_mode == Node::INTERNAL_MODE_BACK ? back : external)).push_back(child);
	};
	auto remove = [&](LocalVector<Node *> &p_list, uint32_t p_at) {
		Node *child = p_list[p_at];
		parent->remove_child(child);
		p_list.remove_at(p_at);
		memdelete(child);
	};

	for (int i = 0; i < 40; i++) {
		add(Node::INTERNAL_MODE_DISABLED);
	}
	check();
	// Several removals before any read (one compaction), then reads.
	remove(external, 3);
	remove(external, 10);
	remove(external, 0);
	remove(external, external.size() - 1); // The last one: popped, not a hole.
	check();
	// Removal, then an addition before the next read.
	remove(external, 7);
	add(Node::INTERNAL_MODE_DISABLED);
	check();
	// Internal children on both sides, removals in each section.
	for (int i = 0; i < 4; i++) {
		add(Node::INTERNAL_MODE_FRONT);
		add(Node::INTERNAL_MODE_BACK);
	}
	check();
	remove(front, 1);
	remove(back, 0);
	remove(external, 5);
	check();
	// A move after removals, then removals after the move.
	remove(external, 2);
	parent->move_child(external[external.size() - 1], 0);
	Node *moved = external[external.size() - 1];
	external.remove_at(external.size() - 1);
	external.insert(0, moved);
	check();
	remove(external, 4);
	remove(external, 1);
	check();
	// Removed and added back before a read.
	Node *again = external[6];
	parent->remove_child(again);
	external.remove_at(6);
	parent->add_child(again);
	external.push_back(again);
	check();

	memdelete(parent);
}

TEST_CASE("[SceneTree][Node] Duplicating node with internal children") {
	GDREGISTER_CLASS(TestNode);

	TestNode *node = memnew(TestNode);
	Node *child = memnew(Node);
	child->set_name("Child");
	node->add_child(child);

	int child_count = node->get_child_count();

	Node *dup = node->duplicate();
	CHECK(dup->get_child_count() == child_count);
	CHECK(dup->has_node(String("Child")));

	memdelete(node);
	memdelete(dup);
}

TEST_CASE("[SceneTree][Node]Exported node checks") {
	TestNode *node = memnew(TestNode);
	SceneTree::get_singleton()->get_root()->add_child(node);

	Node *child = memnew(Node);
	child->set_name("Child");
	node->add_child(child);
	child->set_owner(node);

	Node *child2 = memnew(Node);
	child2->set_name("Child2");
	node->add_child(child2);
	child2->set_owner(node);

	Array children;
	children.append(child);

	node->set("exported_node", child);
	node->set("exported_nodes", children);

	SUBCASE("Property of duplicated node should point to duplicated child") {
		GDREGISTER_CLASS(TestNode);

		TestNode *dup = Object::cast_to<TestNode>(node->duplicate());
		Node *new_exported = Object::cast_to<Node>(dup->get("exported_node"));
		CHECK(new_exported == dup->get_child(0, false));

		memdelete(dup);
	}

#ifdef TOOLS_ENABLED
	SUBCASE("Saving instance with exported nodes should not store the unchanged property") {
		Ref<PackedScene> ps;
		ps.instantiate();
		ps->pack(node);

		String scene_path = TestUtils::get_temp_path("test_scene.tscn");
		ps->set_path(scene_path);

		Node *root = memnew(Node);

		Node *sub_child = ps->instantiate(PackedScene::GEN_EDIT_STATE_MAIN);
		root->add_child(sub_child);
		sub_child->set_owner(root);

		Ref<PackedScene> ps2;
		ps2.instantiate();
		ps2->pack(root);

		scene_path = TestUtils::get_temp_path("new_test_scene.tscn");
		ResourceSaver::save(ps2, scene_path);
		memdelete(root);

		bool is_wrong = false;
		Ref<FileAccess> fa = FileAccess::open(scene_path, FileAccess::READ);
		while (!fa->eof_reached()) {
			const String line = fa->get_line();
			if (line.begins_with("exported_node")) {
				// The property was saved, while it shouldn't.
				is_wrong = true;
				break;
			}
		}
		CHECK_FALSE(is_wrong);
	}

	SUBCASE("Saving instance with exported nodes should store property if changed") {
		Ref<PackedScene> ps;
		ps.instantiate();
		ps->pack(node);

		String scene_path = TestUtils::get_temp_path("test_scene.tscn");
		ps->set_path(scene_path);

		Node *root = memnew(Node);

		Node *sub_child = ps->instantiate(PackedScene::GEN_EDIT_STATE_MAIN);
		root->add_child(sub_child);
		sub_child->set_owner(root);

		sub_child->set("exported_node", sub_child->get_child(1, false));

		children = Array();
		children.append(sub_child->get_child(1, false));
		sub_child->set("exported_nodes", children);

		Ref<PackedScene> ps2;
		ps2.instantiate();
		ps2->pack(root);

		scene_path = TestUtils::get_temp_path("new_test_scene2.tscn");
		ResourceSaver::save(ps2, scene_path);
		memdelete(root);

		int stored_properties = 0;
		Ref<FileAccess> fa = FileAccess::open(scene_path, FileAccess::READ);
		while (!fa->eof_reached()) {
			const String line = fa->get_line();
			if (line.begins_with("exported_node")) {
				stored_properties++;
			}
		}
		CHECK_EQ(stored_properties, 2);
	}
#endif // TOOLS_ENABLED

	memdelete(node);
}

TEST_CASE("[Node] Processing checks") {
	Node *node = memnew(Node);

	SUBCASE("Processing") {
		CHECK_FALSE(node->is_processing());

		node->set_process(true);

		CHECK(node->is_processing());

		node->set_process(false);

		CHECK_FALSE(node->is_processing());
	}

	SUBCASE("Physics processing") {
		CHECK_FALSE(node->is_physics_processing());

		node->set_physics_process(true);

		CHECK(node->is_physics_processing());

		node->set_physics_process(false);

		CHECK_FALSE(node->is_physics_processing());
	}

	SUBCASE("Unhandled input processing") {
		CHECK_FALSE(node->is_processing_unhandled_input());

		node->set_process_unhandled_input(true);

		CHECK(node->is_processing_unhandled_input());

		node->set_process_unhandled_input(false);

		CHECK_FALSE(node->is_processing_unhandled_input());
	}

	SUBCASE("Input processing") {
		CHECK_FALSE(node->is_processing_input());

		node->set_process_input(true);

		CHECK(node->is_processing_input());

		node->set_process_input(false);

		CHECK_FALSE(node->is_processing_input());
	}

	SUBCASE("Unhandled key input processing") {
		CHECK_FALSE(node->is_processing_unhandled_key_input());

		node->set_process_unhandled_key_input(true);

		CHECK(node->is_processing_unhandled_key_input());

		node->set_process_unhandled_key_input(false);

		CHECK_FALSE(node->is_processing_unhandled_key_input());
	}

	SUBCASE("Shortcut input processing") {
		CHECK_FALSE(node->is_processing_shortcut_input());

		node->set_process_shortcut_input(true);

		CHECK(node->is_processing_shortcut_input());

		node->set_process_shortcut_input(false);

		CHECK_FALSE(node->is_processing_shortcut_input());
	}

	SUBCASE("Internal processing") {
		CHECK_FALSE(node->is_processing_internal());

		node->set_process_internal(true);

		CHECK(node->is_processing_internal());

		node->set_process_internal(false);

		CHECK_FALSE(node->is_processing_internal());
	}

	SUBCASE("Process priority") {
		CHECK_EQ(0, node->get_process_priority());

		node->set_process_priority(1);

		CHECK_EQ(1, node->get_process_priority());
	}

	SUBCASE("Physics process priority") {
		CHECK_EQ(0, node->get_physics_process_priority());

		node->set_physics_process_priority(1);

		CHECK_EQ(1, node->get_physics_process_priority());
	}

	memdelete(node);
}

TEST_CASE("[SceneTree][Node] Test the processing") {
	TestNode *node = memnew(TestNode);
	SceneTree::get_singleton()->get_root()->add_child(node);

	SUBCASE("No process") {
		CHECK_EQ(0, node->process_counter);
		CHECK_EQ(0, node->physics_process_counter);
	}

	SUBCASE("Process") {
		node->set_process(true);
		SceneTree::get_singleton()->process(0);

		CHECK_EQ(1, node->process_counter);
		CHECK_EQ(0, node->physics_process_counter);
		CHECK_EQ(0, node->internal_process_counter);
		CHECK_EQ(0, node->internal_physics_process_counter);
	}

	SUBCASE("Physics process") {
		node->set_physics_process(true);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(0, node->process_counter);
		CHECK_EQ(1, node->physics_process_counter);
		CHECK_EQ(0, node->internal_process_counter);
		CHECK_EQ(0, node->internal_physics_process_counter);
	}

	SUBCASE("Normal and physics process") {
		node->set_process(true);
		node->set_physics_process(true);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(1, node->process_counter);
		CHECK_EQ(1, node->physics_process_counter);
		CHECK_EQ(0, node->internal_process_counter);
		CHECK_EQ(0, node->internal_physics_process_counter);
	}

	SUBCASE("Internal, normal and physics process") {
		node->set_process_internal(true);
		node->set_physics_process_internal(true);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(0, node->process_counter);
		CHECK_EQ(0, node->physics_process_counter);
		CHECK_EQ(1, node->internal_process_counter);
		CHECK_EQ(1, node->internal_physics_process_counter);
	}

	SUBCASE("All processing") {
		node->set_process(true);
		node->set_physics_process(true);
		node->set_process_internal(true);
		node->set_physics_process_internal(true);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(1, node->process_counter);
		CHECK_EQ(1, node->physics_process_counter);
		CHECK_EQ(1, node->internal_process_counter);
		CHECK_EQ(1, node->internal_physics_process_counter);
	}

	SUBCASE("All processing twice") {
		node->set_process(true);
		node->set_physics_process(true);
		node->set_process_internal(true);
		node->set_physics_process_internal(true);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(2, node->process_counter);
		CHECK_EQ(2, node->physics_process_counter);
		CHECK_EQ(2, node->internal_process_counter);
		CHECK_EQ(2, node->internal_physics_process_counter);
	}

	SUBCASE("Enable and disable processing") {
		node->set_process(true);
		node->set_physics_process(true);
		node->set_process_internal(true);
		node->set_physics_process_internal(true);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		node->set_process(false);
		node->set_physics_process(false);
		node->set_process_internal(false);
		node->set_physics_process_internal(false);
		SceneTree::get_singleton()->process(0);
		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(1, node->process_counter);
		CHECK_EQ(1, node->physics_process_counter);
		CHECK_EQ(1, node->internal_process_counter);
		CHECK_EQ(1, node->internal_physics_process_counter);
	}

	memdelete(node);
}

TEST_CASE("[SceneTree][Node] Test the process priority") {
	List<Node *> process_order;

	TestNode *node = memnew(TestNode);
	node->callback_list = &process_order;
	SceneTree::get_singleton()->get_root()->add_child(node);

	TestNode *node2 = memnew(TestNode);
	node2->callback_list = &process_order;
	SceneTree::get_singleton()->get_root()->add_child(node2);

	TestNode *node3 = memnew(TestNode);
	node3->callback_list = &process_order;
	SceneTree::get_singleton()->get_root()->add_child(node3);

	TestNode *node4 = memnew(TestNode);
	node4->callback_list = &process_order;
	SceneTree::get_singleton()->get_root()->add_child(node4);

	SUBCASE("Process priority") {
		node->set_process(true);
		node->set_process_priority(20);
		node2->set_process(true);
		node2->set_process_priority(10);
		node3->set_process(true);
		node3->set_process_priority(40);
		node4->set_process(true);
		node4->set_process_priority(30);

		SceneTree::get_singleton()->process(0);

		CHECK_EQ(4, process_order.size());
		List<Node *>::Element *E = process_order.front();
		CHECK_EQ(E->get(), node2);
		E = E->next();
		CHECK_EQ(E->get(), node);
		E = E->next();
		CHECK_EQ(E->get(), node4);
		E = E->next();
		CHECK_EQ(E->get(), node3);
	}

	SUBCASE("Physics process priority") {
		node->set_physics_process(true);
		node->set_physics_process_priority(20);
		node2->set_physics_process(true);
		node2->set_physics_process_priority(10);
		node3->set_physics_process(true);
		node3->set_physics_process_priority(40);
		node4->set_physics_process(true);
		node4->set_physics_process_priority(30);

		SceneTree::get_singleton()->physics_process(0);

		CHECK_EQ(4, process_order.size());
		List<Node *>::Element *E = process_order.front();
		CHECK_EQ(E->get(), node2);
		E = E->next();
		CHECK_EQ(E->get(), node);
		E = E->next();
		CHECK_EQ(E->get(), node4);
		E = E->next();
		CHECK_EQ(E->get(), node3);
	}

	memdelete(node);
	memdelete(node2);
	memdelete(node3);
	memdelete(node4);
}

} // namespace TestNode
