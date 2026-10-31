/**************************************************************************/
/*  physx_lens_flare_3d.cpp                                               */
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

#include "physx_lens_flare_3d.h"

#include "physx_lens_flare_effect.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"

void PhysXLensFlare3D::_attach() {
	if (attached_world.is_valid() || !is_inside_world()) {
		return;
	}
	World3D *world = get_world_3d().ptr();
	if (world == nullptr) {
		return;
	}
	PhysXLensFlareEffect::attach_world(world);
	attached_world = world->get_instance_id();
}

void PhysXLensFlare3D::_detach() {
	PhysXLensFlareEffect::remove_flare(get_instance_id());
	if (attached_world.is_valid()) {
		PhysXLensFlareEffect::detach_world(attached_world);
		attached_world = ObjectID();
	}
}

// This frame's settings, for the render thread; nothing drawn while hidden.
void PhysXLensFlare3D::_publish() {
	if (settings.is_null() && fallback.is_null()) {
		fallback.instantiate();
	}
	const PhysXLensFlareSettings *look = settings.is_valid() ? settings.ptr() : fallback.ptr();
	if (!is_visible_in_tree() || look->get_intensity() <= 0.0f) {
		PhysXLensFlareEffect::remove_flare(get_instance_id());
		return;
	}
	PhysXLensFlareEffect::Snapshot s;
	s.color = look->get_tint() * look->get_intensity();
	s.threshold = look->get_threshold();
	s.threshold_range = look->get_threshold_range();
	s.ghost_intensity = look->get_ghost_intensity();
	s.ghost_chroma_shift = look->get_ghost_chroma_shift();
	// A ghost is a colour and a scale; one without the other isn't drawn.
	const PackedColorArray colors = look->get_ghost_colors();
	const PackedFloat32Array scales = look->get_ghost_scales();
	s.ghost_count = MIN(MIN(colors.size(), scales.size()), (int64_t)PhysXLensFlareEffect::MAX_GHOSTS);
	for (int i = 0; i < s.ghost_count; i++) {
		s.ghost_colors[i] = colors[i];
		s.ghost_scales[i] = scales[i];
	}
	s.halo_intensity = look->get_halo_intensity();
	s.halo_width = look->get_halo_width();
	s.halo_mask = look->get_halo_mask();
	s.halo_compression = look->get_halo_compression();
	s.halo_chroma_shift = look->get_halo_chroma_shift();
	PhysXLensFlareEffect::set_flare(get_instance_id(), s);
}

void PhysXLensFlare3D::_notification(int p_what) {
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
			// Every frame: the user may swap compositors, or edit the settings.
			_publish();
			PhysXLensFlareEffect::install(get_world_3d().ptr(), get_viewport() ? get_viewport()->get_camera_3d() : nullptr);
		} break;
	}
}

void PhysXLensFlare3D::set_settings(const Ref<PhysXLensFlareSettings> &p_settings) {
	settings = p_settings;
}

void PhysXLensFlare3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_settings", "settings"), &PhysXLensFlare3D::set_settings);
	ClassDB::bind_method(D_METHOD("get_settings"), &PhysXLensFlare3D::get_settings);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "settings", PROPERTY_HINT_RESOURCE_TYPE, "PhysXLensFlareSettings"), "set_settings", "get_settings");
}
