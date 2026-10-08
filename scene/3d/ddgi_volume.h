/**************************************************************************/
/*  ddgi_volume.h                                                         */
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

#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/environment.h"

class DDGIVolume : public VisualInstance3D {
	GDCLASS(DDGIVolume, VisualInstance3D);

	bool enabled = true;
	Vector3 size = Vector3(24, 12, 24);
	float probe_spacing = 1.0f;
	int cascades = 3;
	float energy = 1.0f;
	float normal_bias = 0.1f;
	float view_bias = 0.3f;
	float hysteresis = 0.95f;
	bool probe_relocation = true;
	bool probe_classification = true;
	bool follow_camera = false;
	Environment::DDGIDebugMode debug_mode = Environment::DDGI_DEBUG_DISABLED;
	Ref<Environment> applied_environment;

	Vector3i _grid_from_size() const;
	Ref<Environment> _get_environment() const;
	void _apply_to_environment();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_probe_spacing(float p_spacing);
	float get_probe_spacing() const;

	void set_cascades(int p_cascades);
	int get_cascades() const;

	void set_energy(float p_energy);
	float get_energy() const;

	void set_normal_bias(float p_bias);
	float get_normal_bias() const;

	void set_view_bias(float p_bias);
	float get_view_bias() const;

	void set_hysteresis(float p_hysteresis);
	float get_hysteresis() const;

	void set_probe_relocation(bool p_enabled);
	bool is_probe_relocation_enabled() const;

	void set_probe_classification(bool p_enabled);
	bool is_probe_classification_enabled() const;

	void set_follow_camera(bool p_enabled);
	bool is_following_camera() const;

	void set_debug_mode(Environment::DDGIDebugMode p_mode);
	Environment::DDGIDebugMode get_debug_mode() const;

	virtual AABB get_aabb() const override;
	PackedStringArray get_configuration_warnings() const override;

	DDGIVolume();
};
