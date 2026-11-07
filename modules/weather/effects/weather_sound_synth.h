/**************************************************************************/
/*  weather_sound_synth.h                                                 */
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

#include "core/templates/vector.h"

// The weather's sounds, synthesized from noise, so the engine ships no audio
// files. Each sound is the same every time (fixed seeds).
//
// Loops (rain, wind) are noise shaped in the frequency domain over the whole
// buffer, so they repeat seamlessly; drops that run past the end wrap around
// to the start. Rain is rain on open ground: a wash, a fine crackle of
// countless tiny ticks and a few soft taps, with no ringing tones (those come
// from air bubbles under water and make rain sound like it falls on a pool).
// Thunder follows how real thunder sounds: a near strike starts with a sharp,
// tearing crack, then a heavy boom and a long rolling rumble (sound from
// different parts of the kilometers-long channel arriving at different
// times); a far strike is only the low rumble, because the air absorbs the
// high frequencies over distance.
namespace WeatherSoundSynth {

enum Sound {
	RAIN_LIGHT,
	RAIN_HEAVY,
	WIND,
	THUNDER_FAR_1,
	THUNDER_FAR_2,
	THUNDER_MEDIUM,
	THUNDER_NEAR_1,
	THUNDER_NEAR_2,
	SOUND_MAX,
};

constexpr int MIX_RATE = 44100;
// Loops are 2^19 samples (11.9 s), so the FFT covers them exactly.
constexpr int LOOP_SAMPLES = 1 << 19;

bool is_loop(Sound p_sound);
// 16-bit signed little-endian mono samples at MIX_RATE.
Vector<uint8_t> generate(Sound p_sound);

} // namespace WeatherSoundSynth
