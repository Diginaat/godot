/**************************************************************************/
/*  physx_light_shafts_3d.h                                               */
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

#include "scene/3d/node_3d.h"

class DirectionalLight3D;

// Light shafts (god rays) from a DirectionalLight3D (the sun), drawn in screen
// space: no fog needed, at any distance. Where the sky shows near the sun its
// light streams outward in rays (bloom); where something blocks it, the frame
// is darkened along the same rays (occlusion). Whatever is in the depth buffer
// blocks the light (geometry, a PhysXParticleFluid3D's screen-space liquid);
// PhysXFlow3D's gas by how much it darkens what's behind it.
class PhysXLightShafts3D : public Node3D {
	GDCLASS(PhysXLightShafts3D, Node3D);

public:
	enum ShaftMode {
		SHAFTS_BLOOM_AND_OCCLUSION,
		SHAFTS_BLOOM,
		SHAFTS_OCCLUSION,
	};

private:
	NodePath light;
	ShaftMode mode = SHAFTS_BLOOM_AND_OCCLUSION;
	float shaft_bloom_scale = 4.0f;
	float shaft_bloom_threshold = 0.75f;
	Color shaft_bloom_tint = Color(1, 1, 1);
	float shaft_bloom_max_brightness = 8.0f;
	float shaft_occlusion_strength = 0.6f;
	float shaft_occlusion_depth_range = 1000.0f;
	float ray_length = 0.6f;
	float edge_fade = 0.3f;

	ObjectID attached_world;

	DirectionalLight3D *_get_light() const;
	void _attach();
	void _detach();
	void _publish();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_light(const NodePath &p_light);
	NodePath get_light() const { return light; }
	void set_mode(ShaftMode p_mode);
	ShaftMode get_mode() const { return mode; }
	void set_shaft_bloom_scale(float p_scale);
	float get_shaft_bloom_scale() const { return shaft_bloom_scale; }
	void set_shaft_bloom_threshold(float p_threshold);
	float get_shaft_bloom_threshold() const { return shaft_bloom_threshold; }
	void set_shaft_bloom_tint(const Color &p_tint);
	Color get_shaft_bloom_tint() const { return shaft_bloom_tint; }
	void set_shaft_bloom_max_brightness(float p_brightness);
	float get_shaft_bloom_max_brightness() const { return shaft_bloom_max_brightness; }
	void set_shaft_occlusion_strength(float p_strength);
	float get_shaft_occlusion_strength() const { return shaft_occlusion_strength; }
	void set_shaft_occlusion_depth_range(float p_range);
	float get_shaft_occlusion_depth_range() const { return shaft_occlusion_depth_range; }
	void set_ray_length(float p_length);
	float get_ray_length() const { return ray_length; }
	void set_edge_fade(float p_fade);
	float get_edge_fade() const { return edge_fade; }

	PackedStringArray get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(PhysXLightShafts3D::ShaftMode);
