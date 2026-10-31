/**************************************************************************/
/*  physx_light_shafts_3d.cpp                                             */
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

#include "physx_light_shafts_3d.h"

#include "physx_light_shafts_effect.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"

DirectionalLight3D *PhysXLightShafts3D::_get_light() const {
	if (light.is_empty()) {
		return Object::cast_to<DirectionalLight3D>(get_parent());
	}
	return Object::cast_to<DirectionalLight3D>(get_node_or_null(light));
}

void PhysXLightShafts3D::_attach() {
	if (attached_world.is_valid() || !is_inside_world()) {
		return;
	}
	World3D *world = get_world_3d().ptr();
	if (world == nullptr) {
		return;
	}
	PhysXLightShaftsEffect::attach_world(world);
	attached_world = world->get_instance_id();
}

void PhysXLightShafts3D::_detach() {
	PhysXLightShaftsEffect::remove_shafts(get_instance_id());
	if (attached_world.is_valid()) {
		PhysXLightShaftsEffect::detach_world(attached_world);
		attached_world = ObjectID();
	}
}

// This frame's settings and sun direction, for the render thread; nothing
// drawn while this node or its light is hidden.
void PhysXLightShafts3D::_publish() {
	DirectionalLight3D *sun = _get_light();
	if (sun == nullptr || !sun->is_visible_in_tree() || !is_visible_in_tree()) {
		PhysXLightShaftsEffect::remove_shafts(get_instance_id());
		return;
	}
	PhysXLightShaftsEffect::Snapshot s;
	s.light_direction = -sun->get_global_transform_interpolated().basis.get_column(2);
	const bool bloom = mode != SHAFTS_OCCLUSION;
	const bool occlusion = mode != SHAFTS_BLOOM;
	s.bloom_color = bloom ? shaft_bloom_tint * shaft_bloom_scale : Color(0, 0, 0);
	// Relative to the light: the sun's disk in the sky is about as bright as its energy.
	s.bloom_threshold = shaft_bloom_threshold * sun->get_param(Light3D::PARAM_ENERGY);
	s.bloom_max_brightness = shaft_bloom_max_brightness;
	s.occlusion_strength = occlusion ? shaft_occlusion_strength : 0.0f;
	s.occlusion_depth_range = shaft_occlusion_depth_range;
	s.ray_length = ray_length;
	s.edge_fade = edge_fade;
	PhysXLightShaftsEffect::set_shafts(get_instance_id(), s);
}

void PhysXLightShafts3D::_notification(int p_what) {
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
			// Every frame: the sun may move, and the user may swap compositors.
			_publish();
			PhysXLightShaftsEffect::install(get_world_3d().ptr(), get_viewport() ? get_viewport()->get_camera_3d() : nullptr);
		} break;
		case NOTIFICATION_PARENTED:
		case NOTIFICATION_UNPARENTED: {
			update_configuration_warnings();
		} break;
	}
}

void PhysXLightShafts3D::set_light(const NodePath &p_light) {
	light = p_light;
	update_configuration_warnings();
}

void PhysXLightShafts3D::set_mode(ShaftMode p_mode) {
	mode = p_mode;
}

void PhysXLightShafts3D::set_shaft_bloom_scale(float p_scale) {
	shaft_bloom_scale = MAX(p_scale, 0.0f);
}

void PhysXLightShafts3D::set_shaft_bloom_threshold(float p_threshold) {
	shaft_bloom_threshold = MAX(p_threshold, 0.0f);
}

void PhysXLightShafts3D::set_shaft_bloom_tint(const Color &p_tint) {
	shaft_bloom_tint = p_tint;
}

void PhysXLightShafts3D::set_shaft_bloom_max_brightness(float p_brightness) {
	shaft_bloom_max_brightness = MAX(p_brightness, 0.0f);
}

void PhysXLightShafts3D::set_shaft_occlusion_strength(float p_strength) {
	shaft_occlusion_strength = CLAMP(p_strength, 0.0f, 1.0f);
}

void PhysXLightShafts3D::set_shaft_occlusion_depth_range(float p_range) {
	shaft_occlusion_depth_range = MAX(p_range, 0.01f);
}

void PhysXLightShafts3D::set_ray_length(float p_length) {
	ray_length = CLAMP(p_length, 0.0f, 2.0f);
}

void PhysXLightShafts3D::set_edge_fade(float p_fade) {
	edge_fade = MAX(p_fade, 0.0f);
}

PackedStringArray PhysXLightShafts3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (is_inside_tree() && _get_light() == nullptr) {
		warnings.push_back(RTR("Needs a DirectionalLight3D: set \"light\", or make this node a child of one."));
	}
	return warnings;
}

void PhysXLightShafts3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_light", "light"), &PhysXLightShafts3D::set_light);
	ClassDB::bind_method(D_METHOD("get_light"), &PhysXLightShafts3D::get_light);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &PhysXLightShafts3D::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &PhysXLightShafts3D::get_mode);
	ClassDB::bind_method(D_METHOD("set_shaft_bloom_scale", "scale"), &PhysXLightShafts3D::set_shaft_bloom_scale);
	ClassDB::bind_method(D_METHOD("get_shaft_bloom_scale"), &PhysXLightShafts3D::get_shaft_bloom_scale);
	ClassDB::bind_method(D_METHOD("set_shaft_bloom_threshold", "threshold"), &PhysXLightShafts3D::set_shaft_bloom_threshold);
	ClassDB::bind_method(D_METHOD("get_shaft_bloom_threshold"), &PhysXLightShafts3D::get_shaft_bloom_threshold);
	ClassDB::bind_method(D_METHOD("set_shaft_bloom_tint", "tint"), &PhysXLightShafts3D::set_shaft_bloom_tint);
	ClassDB::bind_method(D_METHOD("get_shaft_bloom_tint"), &PhysXLightShafts3D::get_shaft_bloom_tint);
	ClassDB::bind_method(D_METHOD("set_shaft_bloom_max_brightness", "brightness"), &PhysXLightShafts3D::set_shaft_bloom_max_brightness);
	ClassDB::bind_method(D_METHOD("get_shaft_bloom_max_brightness"), &PhysXLightShafts3D::get_shaft_bloom_max_brightness);
	ClassDB::bind_method(D_METHOD("set_shaft_occlusion_strength", "strength"), &PhysXLightShafts3D::set_shaft_occlusion_strength);
	ClassDB::bind_method(D_METHOD("get_shaft_occlusion_strength"), &PhysXLightShafts3D::get_shaft_occlusion_strength);
	ClassDB::bind_method(D_METHOD("set_shaft_occlusion_depth_range", "range"), &PhysXLightShafts3D::set_shaft_occlusion_depth_range);
	ClassDB::bind_method(D_METHOD("get_shaft_occlusion_depth_range"), &PhysXLightShafts3D::get_shaft_occlusion_depth_range);
	ClassDB::bind_method(D_METHOD("set_ray_length", "length"), &PhysXLightShafts3D::set_ray_length);
	ClassDB::bind_method(D_METHOD("get_ray_length"), &PhysXLightShafts3D::get_ray_length);
	ClassDB::bind_method(D_METHOD("set_edge_fade", "fade"), &PhysXLightShafts3D::set_edge_fade);
	ClassDB::bind_method(D_METHOD("get_edge_fade"), &PhysXLightShafts3D::get_edge_fade);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "light", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_light", "get_light");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Shaft Bloom and Occlusion,Shaft Bloom,Shaft Occlusion"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ray_length", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_ray_length", "get_ray_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_fade", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_edge_fade", "get_edge_fade");
	ADD_GROUP("Shaft Bloom", "shaft_bloom_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_bloom_scale", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_shaft_bloom_scale", "get_shaft_bloom_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_bloom_threshold", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_shaft_bloom_threshold", "get_shaft_bloom_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "shaft_bloom_tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_shaft_bloom_tint", "get_shaft_bloom_tint");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_bloom_max_brightness", PROPERTY_HINT_RANGE, "0,32,0.1,or_greater"), "set_shaft_bloom_max_brightness", "get_shaft_bloom_max_brightness");
	ADD_GROUP("Shaft Occlusion", "shaft_occlusion_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_occlusion_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_shaft_occlusion_strength", "get_shaft_occlusion_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shaft_occlusion_depth_range", PROPERTY_HINT_RANGE, "1,10000,1,or_greater,exp,suffix:m"), "set_shaft_occlusion_depth_range", "get_shaft_occlusion_depth_range");

	BIND_ENUM_CONSTANT(SHAFTS_BLOOM_AND_OCCLUSION);
	BIND_ENUM_CONSTANT(SHAFTS_BLOOM);
	BIND_ENUM_CONSTANT(SHAFTS_OCCLUSION);
}
