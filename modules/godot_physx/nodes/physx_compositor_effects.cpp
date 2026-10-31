/**************************************************************************/
/*  physx_compositor_effects.cpp                                          */
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

#include "physx_compositor_effects.h"

#include "scene/3d/camera_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

HashMap<ObjectID, PhysXCompositorEffects::CompositorState> PhysXCompositorEffects::compositors;
HashMap<ObjectID, PhysXCompositorEffects::WorldState> PhysXCompositorEffects::worlds;

namespace {

// The RIDs of the user's own (valid) effects in a compositor, in order;
// whether one of them is of p_class (then the user runs it themselves).
LocalVector<RID> own_effects(const Compositor *p_compositor, const StringName &p_class = StringName(), bool *r_has_class = nullptr) {
	LocalVector<RID> rids;
	const TypedArray<CompositorEffect> effects = p_compositor->get_compositor_effects();
	for (int i = 0; i < effects.size(); i++) {
		Ref<CompositorEffect> e = effects[i];
		if (e.is_valid()) {
			rids.push_back(e->get_rid());
			if (r_has_class != nullptr && e->get_class_name() == p_class) {
				*r_has_class = true;
			}
		}
	}
	return rids;
}

bool same_rids(const LocalVector<RID> &p_a, const LocalVector<RID> &p_b) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	for (uint32_t i = 0; i < p_a.size(); i++) {
		if (p_a[i] != p_b[i]) {
			return false;
		}
	}
	return true;
}

} // namespace

void PhysXCompositorEffects::_push(Compositor *p_compositor, const LocalVector<RID> &p_own, const CompositorState &p_state) {
	TypedArray<RID> rids;
	for (const RID &r : p_own) {
		rids.push_back(r);
	}
	for (const Placed &p : p_state.ours) {
		rids.push_back(p.effect);
	}
	RenderingServer::get_singleton()->compositor_set_compositor_effects(p_compositor->get_rid(), rids);
}

void PhysXCompositorEffects::_place(const Ref<Compositor> &p_compositor, const Ref<CompositorEffect> &p_effect, int p_order) {
	bool user_has_one = false;
	const LocalVector<RID> own = own_effects(p_compositor.ptr(), p_effect->get_class_name(), &user_has_one);
	if (user_has_one) {
		return; // the user added one themselves
	}
	CompositorState &state = compositors[p_compositor->get_instance_id()];
	bool changed = !same_rids(own, state.seen);
	bool found = false;
	for (const Placed &p : state.ours) {
		found = found || p.effect == p_effect->get_rid();
	}
	if (!found) {
		uint32_t at = 0;
		while (at < state.ours.size() && state.ours[at].order <= p_order) {
			at++;
		}
		state.ours.insert(at, Placed{ p_order, p_effect->get_rid() });
		changed = true;
	}
	if (changed) {
		_push(p_compositor.ptr(), own, state);
		state.seen = own;
	}
}

void PhysXCompositorEffects::_withdraw(const ObjectID &p_compositor, const RID &p_effect) {
	CompositorState *state = compositors.getptr(p_compositor);
	if (state == nullptr) {
		return;
	}
	for (uint32_t i = 0; i < state->ours.size(); i++) {
		if (state->ours[i].effect == p_effect) {
			state->ours.remove_at(i);
			break;
		}
	}
	Compositor *compositor = Object::cast_to<Compositor>(ObjectDB::get_instance(p_compositor));
	if (compositor != nullptr) {
		const LocalVector<RID> own = own_effects(compositor);
		_push(compositor, own, *state);
		state->seen = own;
	}
	if (compositor == nullptr || state->ours.is_empty()) {
		compositors.erase(p_compositor);
	}
}

void PhysXCompositorEffects::install(World3D *p_world, Camera3D *p_camera, const Ref<CompositorEffect> &p_effect, int p_order) {
	ERR_FAIL_NULL(p_world);
	ERR_FAIL_COND(p_effect.is_null());
	WorldState &state = worlds[p_world->get_instance_id()];
	state.effects.insert(p_effect->get_rid());
	Ref<Compositor> compositor = p_world->get_compositor();
	if (compositor.is_null()) {
		if (state.created.is_null()) {
			state.created.instantiate();
		}
		p_world->set_compositor(state.created);
		compositor = state.created;
	}
	_place(compositor, p_effect, p_order);
	state.compositors.insert(compositor->get_instance_id());
	if (p_camera != nullptr && p_camera->get_compositor().is_valid()) {
		_place(p_camera->get_compositor(), p_effect, p_order);
		state.compositors.insert(p_camera->get_compositor()->get_instance_id());
	}
}

void PhysXCompositorEffects::uninstall(const ObjectID &p_world, const Ref<CompositorEffect> &p_effect) {
	WorldState *state = worlds.getptr(p_world);
	if (state == nullptr || p_effect.is_null()) {
		return;
	}
	state->effects.erase(p_effect->get_rid());
	for (const ObjectID &c : state->compositors) {
		_withdraw(c, p_effect->get_rid());
	}
	if (!state->effects.is_empty()) {
		return;
	}
	World3D *world = Object::cast_to<World3D>(ObjectDB::get_instance(p_world));
	if (world != nullptr && state->created.is_valid() && world->get_compositor() == state->created) {
		world->set_compositor(Ref<Compositor>());
	}
	worlds.erase(p_world);
}
