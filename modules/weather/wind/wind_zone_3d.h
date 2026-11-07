/**************************************************************************/
/*  wind_zone_3d.h                                                        */
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

// Local wind on top of the world's wind, in a sphere or a box: a fan or a
// wind tunnel (directional), a rotor's downwash or a blast (radial), a dust
// devil (vortex), or a sheltered spot (replace with strength 0). Everything
// that follows the WindServer feels it: foliage through the wind shader
// include, rigid bodies through WindReceiver3D, cloth, and scripts calling
// WindServer.sample().
class WindZone3D : public Node3D {
	GDCLASS(WindZone3D, Node3D);

public:
	enum Mode {
		MODE_DIRECTIONAL,
		MODE_RADIAL,
		MODE_VORTEX,
	};

	enum Shape {
		SHAPE_SPHERE,
		SHAPE_BOX,
	};

private:
	bool enabled = true;
	Mode mode = MODE_DIRECTIONAL;
	Shape shape = SHAPE_SPHERE;
	float radius = 10.0f;
	Vector3 size = Vector3(10.0f, 10.0f, 10.0f);
	float strength = 10.0f;
	float falloff = 0.25f;
	bool replace_global = false;

	bool registered = false;

	void _update_zone();

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_mode(Mode p_mode);
	Mode get_mode() const;
	void set_shape(Shape p_shape);
	Shape get_shape() const;
	void set_radius(float p_radius);
	float get_radius() const;
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;
	void set_strength(float p_strength);
	float get_strength() const;
	void set_falloff(float p_falloff);
	float get_falloff() const;
	void set_replace_global(bool p_replace);
	bool is_replacing_global() const;

	Vector3 get_wind_at(const Vector3 &p_position) const;
	bool is_inside(const Vector3 &p_position) const;

	~WindZone3D();
};

VARIANT_ENUM_CAST(WindZone3D::Mode);
VARIANT_ENUM_CAST(WindZone3D::Shape);
