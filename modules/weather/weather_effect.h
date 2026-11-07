/**************************************************************************/
/*  weather_effect.h                                                      */
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
#include "core/object/gdvirtual.gen.h"

class Weather;

// One part of the weather (lighting, sky, rain, haze, lightning, sound...).
// A Weather node runs the effects in its effects list every frame. A new kind
// of weather effect (snow, hail, sand) is a new subclass, in C++ or a script:
// override start() to create its nodes as internal children of the Weather
// node, update() to follow the weather, stop() to free them, and
// apply_quality() for the quality levels. Scene values an effect changes go
// through Weather.drive(), so they are restored before the scene is saved.
//
// Effects keep per-scene state, so they are local to the scene.
class WeatherEffect : public Resource {
	GDCLASS(WeatherEffect, Resource);

	bool enabled = true;

protected:
	static void _bind_methods();

	GDVIRTUAL1(_start, Weather *)
	GDVIRTUAL1(_stop, Weather *)
	GDVIRTUAL2(_update, Weather *, double)
	GDVIRTUAL2(_apply_quality, Weather *, int)

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	virtual void start(Weather *p_weather);
	virtual void stop(Weather *p_weather);
	virtual void update(Weather *p_weather, double p_delta);
	virtual void apply_quality(Weather *p_weather, int p_quality);

	WeatherEffect();
};
