/**************************************************************************/
/*  weather_sky_material.h                                                */
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

#include "scene/resources/material.h"

// The built-in weather sky as a material of its own, so it can be picked
// in a Sky's material list ("New WeatherSkyMaterial"). A Weather node's sky
// effect drives it directly: clouds, overcast and lightning follow the
// weather. Without a Weather node it is a still, partly cloudy sky.
//
// The shader is shared and served from memory at SHADER_PATH, so saved
// materials refer to it by path and pick up shader updates.
class WeatherSkyMaterial : public ShaderMaterial {
	GDCLASS(WeatherSkyMaterial, ShaderMaterial);

protected:
	static void _bind_methods();

public:
	static constexpr const char *SHADER_PATH = "res://.builtin/weather_sky.gdshader";

	static void register_builtin_shader();
	static void free_builtin_shader();
	static Ref<Shader> get_builtin_shader();

	WeatherSkyMaterial();
};
