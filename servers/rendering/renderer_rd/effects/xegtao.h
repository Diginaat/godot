/**************************************************************************/
/*  xegtao.h                                                              */
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

#include "core/math/projection.h"
#include "servers/rendering/renderer_rd/shaders/effects/xegtao_debug.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/xegtao_denoise.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/xegtao_main.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/xegtao_prefilter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/xegtao_upscale.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

#define RB_SCOPE_XEGTAO SNAME("rb_xegtao")

namespace RendererRD {

// XeGTAO (Intel's ground truth ambient occlusion, MIT) as a screen-space
// compute effect: depth MIP chain, horizon search, edge-aware denoise and,
// at half resolution, a depth-guided upscale. Its output is a visibility
// texture (R8, internal resolution, one layer per view) that the Forward+
// renderer binds in place of the SSAO texture. Settings come from the
// DDGIVolume (RendererEnvironmentStorage::DDGISettings ao_*).
class XeGTAO {
public:
	struct Settings {
		int quality = 2; // 0 low, 1 medium, 2 high, 3 ultra.
		int resolution = 0; // 0 auto, 1 full, 2 half.
		float radius = 0.5;
		float intensity = 1.0;
		bool bent_normals = false;
		int debug_mode = 0; // Environment::DDGIAODebugMode.
		bool temporal = false; // Temporal AA or a temporal upscaler accumulates the result.
		uint64_t frame = 0;
	};

private:
	struct PushConstant {
		int32_t size[2];
		float pixel_size[2];

		float ndc_to_view_mul[2];
		float ndc_to_view_add[2];

		float ndc_to_view_mul_x_pixel_size[2];
		float effect_radius;
		float effect_falloff_range;

		float final_value_power;
		float denoise_blur_beta;
		uint32_t noise_index;
		uint32_t orthogonal;

		float depth_unproject[4];

		int32_t full_size[2];
		int32_t scale;
		float sky_depth;

		uint32_t debug_mode;
		uint32_t pad[3];
	};
	static_assert(sizeof(PushConstant) == 112);

	XegtaoPrefilterShaderRD prefilter_shader;
	RID prefilter_version;
	RID prefilter_pipeline;

	// Variant = quality + 4 * bent normals.
	XegtaoMainShaderRD main_shader;
	RID main_version;
	RID main_pipelines[8];

	// Variant = final pass + 2 * bent normals.
	XegtaoDenoiseShaderRD denoise_shader;
	RID denoise_version;
	RID denoise_pipelines[4];

	// Variant = bent normals.
	XegtaoUpscaleShaderRD upscale_shader;
	RID upscale_version;
	RID upscale_pipelines[2];

	XegtaoDebugShaderRD debug_shader;
	RID debug_version;
	RID debug_pipelines[2];

	RID point_sampler;

	static bool _use_half_resolution(const Size2i &p_internal_size, const Settings &p_settings);
	void _ensure_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, const Size2i &p_ao_size, bool p_half, const Settings &p_settings);

public:
	// The AO resolution for these settings, or an empty size when the view
	// is too small to run (the renderer then leaves AO off for the frame).
	static Size2i get_ao_size(const Size2i &p_internal_size, const Settings &p_settings);

	// Runs every pass for every view. p_normal_roughness: one slice per view.
	void process(Ref<RenderSceneBuffersRD> p_render_buffers, const RID *p_normal_roughness, const Projection *p_projections, const Settings &p_settings);

	// Visibility for the scene shader (all views), valid after process().
	static RID get_visibility_texture(Ref<RenderSceneBuffersRD> p_render_buffers);
	// Bent normal slices (one per view), when process() made them; false otherwise.
	static bool get_bent_normal_slices(Ref<RenderSceneBuffersRD> p_render_buffers, RID *r_slices);
	// The debug view, or RID() when none was rendered.
	static RID get_debug_texture(Ref<RenderSceneBuffersRD> p_render_buffers);

	XeGTAO();
	~XeGTAO();
};

} // namespace RendererRD
