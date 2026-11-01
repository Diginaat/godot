/**************************************************************************/
/*  planar_reflector_3d.cpp                                               */
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

#include "planar_reflector_3d.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

void PlanarReflector3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			RS::get_singleton()->planar_reflection_set_scenario(planar_reflection, get_world_3d()->get_scenario());
			RS::get_singleton()->planar_reflection_set_transform(planar_reflection, get_global_transform());
			RS::get_singleton()->planar_reflection_set_visible(planar_reflection, is_visible_in_tree());
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			RS::get_singleton()->planar_reflection_set_scenario(planar_reflection, RID());
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			RS::get_singleton()->planar_reflection_set_transform(planar_reflection, get_global_transform());
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			RS::get_singleton()->planar_reflection_set_visible(planar_reflection, is_visible_in_tree());
		} break;
	}
}

void PlanarReflector3D::set_size(const Vector2 &p_size) {
	size = p_size.maxf(0.0);
	RS::get_singleton()->planar_reflection_set_size(planar_reflection, size);
	update_gizmos();
}

Vector2 PlanarReflector3D::get_size() const {
	return size;
}

void PlanarReflector3D::set_layer_mask(uint32_t p_layers) {
	layers = p_layers;
	RS::get_singleton()->planar_reflection_set_layer_mask(planar_reflection, layers);
}

uint32_t PlanarReflector3D::get_layer_mask() const {
	return layers;
}

void PlanarReflector3D::set_intensity(float p_intensity) {
	intensity = CLAMP(p_intensity, 0.0f, 1.0f);
	RS::get_singleton()->planar_reflection_set_intensity(planar_reflection, intensity);
}

float PlanarReflector3D::get_intensity() const {
	return intensity;
}

void PlanarReflector3D::set_roughness_cutoff(float p_roughness) {
	roughness_cutoff = CLAMP(p_roughness, 0.0f, 1.0f);
	RS::get_singleton()->planar_reflection_set_roughness_cutoff(planar_reflection, roughness_cutoff);
}

float PlanarReflector3D::get_roughness_cutoff() const {
	return roughness_cutoff;
}

void PlanarReflector3D::set_normal_distortion(float p_distortion) {
	normal_distortion = p_distortion;
	RS::get_singleton()->planar_reflection_set_normal_distortion(planar_reflection, normal_distortion);
}

float PlanarReflector3D::get_normal_distortion() const {
	return normal_distortion;
}

void PlanarReflector3D::set_thickness(float p_thickness) {
	thickness = MAX(p_thickness, 0.0f);
	RS::get_singleton()->planar_reflection_set_thickness(planar_reflection, thickness);
}

float PlanarReflector3D::get_thickness() const {
	return thickness;
}

AABB PlanarReflector3D::get_aabb() const {
	return AABB(Vector3(-size.x * 0.5, 0.0, -size.y * 0.5), Vector3(size.x, 0.0, size.y));
}

PackedStringArray PlanarReflector3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();

	if (RenderingServer::get_singleton()->get_current_rendering_method() != "forward_plus") {
		warnings.push_back(RTR("Planar reflections are only supported by the Forward+ renderer."));
	}
	if (!GLOBAL_GET("rendering/global_illumination/reflections/planar_enabled")) {
		warnings.push_back(RTR("Planar reflections are disabled in the project settings (Rendering > Global Illumination > Reflections > Planar Enabled)."));
	}
	if (size.x <= 0.0 || size.y <= 0.0) {
		warnings.push_back(RTR("The reflector has no area: set a size larger than zero."));
	}

	return warnings;
}

void PlanarReflector3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &PlanarReflector3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &PlanarReflector3D::get_size);
	ClassDB::bind_method(D_METHOD("set_layer_mask", "layers"), &PlanarReflector3D::set_layer_mask);
	ClassDB::bind_method(D_METHOD("get_layer_mask"), &PlanarReflector3D::get_layer_mask);
	ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &PlanarReflector3D::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &PlanarReflector3D::get_intensity);
	ClassDB::bind_method(D_METHOD("set_roughness_cutoff", "roughness"), &PlanarReflector3D::set_roughness_cutoff);
	ClassDB::bind_method(D_METHOD("get_roughness_cutoff"), &PlanarReflector3D::get_roughness_cutoff);
	ClassDB::bind_method(D_METHOD("set_normal_distortion", "distortion"), &PlanarReflector3D::set_normal_distortion);
	ClassDB::bind_method(D_METHOD("get_normal_distortion"), &PlanarReflector3D::get_normal_distortion);
	ClassDB::bind_method(D_METHOD("set_thickness", "thickness"), &PlanarReflector3D::set_thickness);
	ClassDB::bind_method(D_METHOD("get_thickness"), &PlanarReflector3D::get_thickness);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_intensity", "get_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness_cutoff", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness_cutoff", "get_roughness_cutoff");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_distortion", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_normal_distortion", "get_normal_distortion");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thickness", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_thickness", "get_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_layer_mask", "get_layer_mask");
}

PlanarReflector3D::PlanarReflector3D() {
	planar_reflection = RS::get_singleton()->planar_reflection_create();
	RS::get_singleton()->planar_reflection_set_size(planar_reflection, size);
	RS::get_singleton()->planar_reflection_set_layer_mask(planar_reflection, layers);
	RS::get_singleton()->planar_reflection_set_intensity(planar_reflection, intensity);
	RS::get_singleton()->planar_reflection_set_roughness_cutoff(planar_reflection, roughness_cutoff);
	RS::get_singleton()->planar_reflection_set_normal_distortion(planar_reflection, normal_distortion);
	RS::get_singleton()->planar_reflection_set_thickness(planar_reflection, thickness);
	set_notify_transform(true);
}

PlanarReflector3D::~PlanarReflector3D() {
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	RS::get_singleton()->free_rid(planar_reflection);
}
