/**************************************************************************/
/*  wind_receiver_3d.h                                                    */
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

class RigidBody3D;

// Pushes its parent RigidBody3D with the WindServer's wind: aerodynamic drag,
// F = 0.5 * air density * drag coefficient * area * |v|^2 along the wind
// relative to the body. The force acts at this node's position, so placing it
// above the body's centre makes tall objects tip over. Under any other parent
// it only computes the force (get_wind_force()), for scripts.
class WindReceiver3D : public Node3D {
	GDCLASS(WindReceiver3D, Node3D);

	float drag_coefficient = 1.0f;
	float area = 0.0f;
	float strength = 1.0f;
	float wake_acceleration = 0.3f;
	bool enabled = true;

	// Body-space size of the collision shapes, for the area facing the wind.
	Vector3 shape_size;
	bool shape_size_dirty = true;
	Vector3 wind_force;
	Vector3 wind_velocity;

	RigidBody3D *_get_body() const;
	void _update_shape_size();
	float _get_area_facing(const Vector3 &p_direction) const;
	void _physics_step(double p_delta);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_drag_coefficient(float p_coefficient);
	float get_drag_coefficient() const;
	void set_area(float p_area);
	float get_area() const;
	void set_strength(float p_strength);
	float get_strength() const;
	void set_wake_acceleration(float p_acceleration);
	float get_wake_acceleration() const;

	Vector3 get_wind_force() const;
	Vector3 get_wind_velocity() const;
	float get_effective_area() const;
	void update_shape_size();

	PackedStringArray get_configuration_warnings() const override;

	WindReceiver3D();
};
