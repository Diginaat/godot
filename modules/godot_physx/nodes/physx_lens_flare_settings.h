/**************************************************************************/
/*  physx_lens_flare_settings.h                                           */
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

#include "core/io/resource.h"
#include "core/variant/variant.h"

// How a PhysXLensFlare3D looks: a Resource, so a look can be saved and shared as a preset. The defaults are a
// warm, eight-ghost camera flare.
class PhysXLensFlareSettings : public Resource {
	GDCLASS(PhysXLensFlareSettings, Resource);

public:
	static constexpr int MAX_GHOSTS = 8;

private:
	float intensity = 1.0f;
	Color tint = Color(1.0, 0.85, 0.7);
	float threshold = 0.8f;
	float threshold_range = 0.5f;

	float ghost_intensity = 1.0f;
	float ghost_chroma_shift = 0.015f;
	PackedColorArray ghost_colors;
	PackedFloat32Array ghost_scales;

	float halo_intensity = 1.0f;
	float halo_width = 0.6f;
	float halo_mask = 0.5f;
	float halo_compression = 0.65f;
	float halo_chroma_shift = 0.015f;

protected:
	static void _bind_methods();

public:
	void set_intensity(float p_value);
	float get_intensity() const { return intensity; }
	void set_tint(const Color &p_value);
	Color get_tint() const { return tint; }
	void set_threshold(float p_value);
	float get_threshold() const { return threshold; }
	void set_threshold_range(float p_value);
	float get_threshold_range() const { return threshold_range; }

	void set_ghost_intensity(float p_value);
	float get_ghost_intensity() const { return ghost_intensity; }
	void set_ghost_chroma_shift(float p_value);
	float get_ghost_chroma_shift() const { return ghost_chroma_shift; }
	void set_ghost_colors(const PackedColorArray &p_value);
	PackedColorArray get_ghost_colors() const { return ghost_colors; }
	void set_ghost_scales(const PackedFloat32Array &p_value);
	PackedFloat32Array get_ghost_scales() const { return ghost_scales; }

	void set_halo_intensity(float p_value);
	float get_halo_intensity() const { return halo_intensity; }
	void set_halo_width(float p_value);
	float get_halo_width() const { return halo_width; }
	void set_halo_mask(float p_value);
	float get_halo_mask() const { return halo_mask; }
	void set_halo_compression(float p_value);
	float get_halo_compression() const { return halo_compression; }
	void set_halo_chroma_shift(float p_value);
	float get_halo_chroma_shift() const { return halo_chroma_shift; }

	PhysXLensFlareSettings();
};
