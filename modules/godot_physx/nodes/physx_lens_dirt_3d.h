/**************************************************************************/
/*  physx_lens_dirt_3d.h                                                  */
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
#include "scene/resources/texture.h"

// Lens dirt, as Unreal's bloom dirt mask: the frame's bright light, spread wide the way a lens scatters it, lights a
// dirt texture laid over the screen and is added on top. Any bright light does it -- the sun, fire, headlights, the
// light shafts -- and where the texture is clean (black), or nothing is bright, the frame is untouched.
class PhysXLensDirt3D : public Node3D {
	GDCLASS(PhysXLensDirt3D, Node3D);

	Ref<Texture2D> dirt_texture;
	float intensity = 3.0f;
	Color tint = Color(1, 1, 1);
	float threshold = 0.0f;

	ObjectID attached_world;

	void _attach();
	void _detach();
	void _publish();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_dirt_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_dirt_texture() const { return dirt_texture; }
	void set_intensity(float p_intensity);
	float get_intensity() const { return intensity; }
	void set_tint(const Color &p_tint);
	Color get_tint() const { return tint; }
	void set_threshold(float p_threshold);
	float get_threshold() const { return threshold; }

	PackedStringArray get_configuration_warnings() const override;
};
