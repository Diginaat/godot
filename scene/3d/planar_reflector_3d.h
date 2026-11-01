/**************************************************************************/
/*  planar_reflector_3d.h                                                 */
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

// A rectangle that reflects the scene like a mirror (floors, water, mirrors, glass),
// rendered by pixel projection: two compute passes over the screen, no second scene
// render. Only what is on screen can be reflected; the rest falls back to reflection
// probes and the sky. Forward+ only; independent of DDGI.
class PlanarReflector3D : public Node3D {
	GDCLASS(PlanarReflector3D, Node3D);

	RID planar_reflection;
	Vector2 size = Vector2(10, 10);
	uint32_t layers = 1;
	float intensity = 1.0;
	float roughness_cutoff = 0.4;
	float normal_distortion = 0.0;
	float thickness = 0.05;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const;

	void set_layer_mask(uint32_t p_layers);
	uint32_t get_layer_mask() const;

	void set_intensity(float p_intensity);
	float get_intensity() const;

	void set_roughness_cutoff(float p_roughness);
	float get_roughness_cutoff() const;

	void set_normal_distortion(float p_distortion);
	float get_normal_distortion() const;

	void set_thickness(float p_thickness);
	float get_thickness() const;

	virtual AABB get_aabb() const;
	PackedStringArray get_configuration_warnings() const override;

	PlanarReflector3D();
	~PlanarReflector3D();
};
