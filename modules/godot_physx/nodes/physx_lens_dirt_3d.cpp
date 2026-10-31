/**************************************************************************/
/*  physx_lens_dirt_3d.cpp                                                */
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

#include "physx_lens_dirt_3d.h"

#include "physx_lens_dirt_effect.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"

void PhysXLensDirt3D::_attach() {
	if (attached_world.is_valid() || !is_inside_world()) {
		return;
	}
	World3D *world = get_world_3d().ptr();
	if (world == nullptr) {
		return;
	}
	PhysXLensDirtEffect::attach_world(world);
	attached_world = world->get_instance_id();
}

void PhysXLensDirt3D::_detach() {
	PhysXLensDirtEffect::remove_dirt(get_instance_id());
	if (attached_world.is_valid()) {
		PhysXLensDirtEffect::detach_world(attached_world);
		attached_world = ObjectID();
	}
}

// This frame's settings, for the render thread; nothing drawn while hidden or without a texture.
void PhysXLensDirt3D::_publish() {
	if (dirt_texture.is_null() || !is_visible_in_tree() || intensity <= 0.0f) {
		PhysXLensDirtEffect::remove_dirt(get_instance_id());
		return;
	}
	PhysXLensDirtEffect::Snapshot s;
	s.dirt_texture = dirt_texture->get_rid();
	s.color = tint * intensity;
	s.threshold = threshold;
	PhysXLensDirtEffect::set_dirt(get_instance_id(), s);
}

void PhysXLensDirt3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			_attach();
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			set_process_internal(false);
			_detach();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!attached_world.is_valid()) {
				return;
			}
			// Every frame: the user may swap compositors.
			_publish();
			PhysXLensDirtEffect::install(get_world_3d().ptr(), get_viewport() ? get_viewport()->get_camera_3d() : nullptr);
		} break;
	}
}

void PhysXLensDirt3D::set_dirt_texture(const Ref<Texture2D> &p_texture) {
	dirt_texture = p_texture;
	update_configuration_warnings();
}

void PhysXLensDirt3D::set_intensity(float p_intensity) {
	intensity = MAX(p_intensity, 0.0f);
}

void PhysXLensDirt3D::set_tint(const Color &p_tint) {
	tint = p_tint;
}

void PhysXLensDirt3D::set_threshold(float p_threshold) {
	threshold = MAX(p_threshold, 0.0f);
}

PackedStringArray PhysXLensDirt3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (dirt_texture.is_null()) {
		warnings.push_back(RTR("Needs a dirt texture: smudges and specks on black, 16:9 like the screen (it's stretched to fit)."));
	}
	return warnings;
}

void PhysXLensDirt3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_dirt_texture", "texture"), &PhysXLensDirt3D::set_dirt_texture);
	ClassDB::bind_method(D_METHOD("get_dirt_texture"), &PhysXLensDirt3D::get_dirt_texture);
	ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &PhysXLensDirt3D::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &PhysXLensDirt3D::get_intensity);
	ClassDB::bind_method(D_METHOD("set_tint", "tint"), &PhysXLensDirt3D::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &PhysXLensDirt3D::get_tint);
	ClassDB::bind_method(D_METHOD("set_threshold", "threshold"), &PhysXLensDirt3D::set_threshold);
	ClassDB::bind_method(D_METHOD("get_threshold"), &PhysXLensDirt3D::get_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "dirt_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_dirt_texture", "get_dirt_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_intensity", "get_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "threshold", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_threshold", "get_threshold");
}
