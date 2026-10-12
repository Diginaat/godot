/**************************************************************************/
/*  ddgi_bake_solver.h                                                    */
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

#include "core/variant/dictionary.h"

/// Least-squares refinement of a baked DDGI volume.
///
/// A bake stores each probe's own traced light. Surfaces see the weighted
/// blend of the 8 probes around them, which can be off between probes even
/// when every probe is right. Given a second bake of the same volume with
/// half the probe spacing (its probes include the coarse ones and the
/// points between them), this solves for the coarse irradiance whose blend,
/// with the same weights the renderer uses, best reproduces the fine
/// probes' light, with two regularizers: smoothness between neighboring
/// probes that see each other, and a pull toward each probe's own traced
/// light. One small sparse system per cascade, octahedral texel and color
/// channel, solved by conjugate gradients (after Iwanicki and Sloan,
/// "Precomputed Lighting in Call of Duty: Infinite Warfare").
struct DDGIBakeSolver {
	struct Settings {
		float normal_bias = 0.1f; // DDGIVolume.normal_bias (times the spacing).
		float smoothness = 0.05f; // Weight of the smoothness term.
		float prior = 0.05f; // Weight of the pull toward the traced light.
		int iterations = 48;
	};

	struct Report {
		int samples = 0; // Fine probes used (active, all cascades).
		float residual_before = 0.0f; // RMS of interpolated - fine (square root space).
		float residual_after = 0.0f;
		float mean_change = 0.0f; // Mean relative change of the coarse irradiance.
	};

	/// Returns p_coarse with its irradiance replaced, or an empty Dictionary
	/// when the two bakes don't fit together (r_error says why).
	static Dictionary solve(const Dictionary &p_coarse, const Dictionary &p_fine, const Settings &p_settings, Report *r_report, String *r_error);
};
