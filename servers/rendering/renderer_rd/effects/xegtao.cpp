/**************************************************************************/
/*  xegtao.cpp                                                            */
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

// Host side of XeGTAO. The algorithm and its constants follow Intel's
// reference (https://github.com/GameTechDev/XeGTAO, MIT, Copyright (C)
// 2016-2021 Intel Corporation; vaGTAO.cpp and XeGTAO.h); the shaders are in
// shaders/effects/xegtao_*.glsl.

#include "xegtao.h"

#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_default.h" // IWYU pragma: keep. RENDER_TIMESTAMP macro uses RSG.

using namespace RendererRD;

#define XEGTAO_DEPTH SNAME("depth")
#define XEGTAO_AO_A SNAME("ao_a")
#define XEGTAO_AO_B SNAME("ao_b")
#define XEGTAO_EDGES SNAME("edges")
#define XEGTAO_HALF_VISIBILITY SNAME("half_visibility")
#define XEGTAO_HALF_BENT SNAME("half_bent")
#define XEGTAO_VISIBILITY SNAME("visibility")
#define XEGTAO_BENT SNAME("bent")
#define XEGTAO_DEBUG SNAME("debug")

static const int XEGTAO_DEPTH_MIPS = 5;
// Below this the 5 depth MIPs don't fit; AO is left off.
static const int XEGTAO_MIN_SIZE = 16;

// Slices and steps per slice of the presets, as in Intel's CSGTAOLow..Ultra.
static const int xegtao_slices[4] = { 1, 2, 3, 9 };
static const int xegtao_steps[4] = { 2, 2, 3, 3 };

XeGTAO::XeGTAO() {
	{
		Vector<String> modes;
		modes.push_back("\n");
		prefilter_shader.initialize(modes);
		prefilter_version = prefilter_shader.version_create();
		prefilter_pipeline = RD::get_singleton()->compute_pipeline_create(prefilter_shader.version_get_shader(prefilter_version, 0));
	}
	{
		Vector<String> modes;
		for (int bent = 0; bent < 2; bent++) {
			for (int q = 0; q < 4; q++) {
				String defines = vformat("\n#define SLICE_COUNT %d\n#define STEPS_PER_SLICE %d\n", xegtao_slices[q], xegtao_steps[q]);
				if (bent) {
					defines += "#define BENT_NORMALS\n";
				}
				modes.push_back(defines);
			}
		}
		main_shader.initialize(modes);
		main_version = main_shader.version_create();
		for (int i = 0; i < 8; i++) {
			main_pipelines[i] = RD::get_singleton()->compute_pipeline_create(main_shader.version_get_shader(main_version, i));
		}
	}
	{
		Vector<String> modes;
		modes.push_back("\n");
		modes.push_back("\n#define FINAL_PASS\n");
		modes.push_back("\n#define BENT_NORMALS\n");
		modes.push_back("\n#define BENT_NORMALS\n#define FINAL_PASS\n");
		denoise_shader.initialize(modes);
		denoise_version = denoise_shader.version_create();
		for (int i = 0; i < 4; i++) {
			denoise_pipelines[i] = RD::get_singleton()->compute_pipeline_create(denoise_shader.version_get_shader(denoise_version, i));
		}
	}
	{
		Vector<String> modes;
		modes.push_back("\n");
		modes.push_back("\n#define BENT_NORMALS\n");
		upscale_shader.initialize(modes);
		upscale_version = upscale_shader.version_create();
		for (int i = 0; i < 2; i++) {
			upscale_pipelines[i] = RD::get_singleton()->compute_pipeline_create(upscale_shader.version_get_shader(upscale_version, i));
		}

		debug_shader.initialize(modes);
		debug_version = debug_shader.version_create();
		for (int i = 0; i < 2; i++) {
			debug_pipelines[i] = RD::get_singleton()->compute_pipeline_create(debug_shader.version_get_shader(debug_version, i));
		}
	}

	// Point sampling without the material samplers' mipmap bias (TAA and
	// upscalers lower it), which would shift the explicit depth MIP levels.
	RD::SamplerState ss;
	ss.mag_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.min_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.mip_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	point_sampler = RD::get_singleton()->sampler_create(ss);
}

XeGTAO::~XeGTAO() {
	RD::get_singleton()->free_rid(point_sampler);
	// The pipelines are freed with their shaders.
	prefilter_shader.version_free(prefilter_version);
	main_shader.version_free(main_version);
	denoise_shader.version_free(denoise_version);
	upscale_shader.version_free(upscale_version);
	debug_shader.version_free(debug_version);
}

bool XeGTAO::_use_half_resolution(const Size2i &p_internal_size, const Settings &p_settings) {
	switch (p_settings.resolution) {
		case 1:
			return false;
		case 2:
			return true;
		default:
			// Auto: full resolution up to about 1440p, half above (4K and
			// up), where the main pass dominates and half resolution AO
			// is hard to tell apart after the upscale.
			return int64_t(p_internal_size.x) * p_internal_size.y > int64_t(2560 * 1600);
	}
}

Size2i XeGTAO::get_ao_size(const Size2i &p_internal_size, const Settings &p_settings) {
	Size2i size = p_internal_size;
	if (_use_half_resolution(p_internal_size, p_settings)) {
		Size2i half = (p_internal_size + Size2i(1, 1)) / 2;
		if (half.x >= XEGTAO_MIN_SIZE && half.y >= XEGTAO_MIN_SIZE) {
			size = half;
		}
	}
	if (size.x < XEGTAO_MIN_SIZE || size.y < XEGTAO_MIN_SIZE) {
		return Size2i();
	}
	return size;
}

void XeGTAO::_ensure_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, const Size2i &p_ao_size, bool p_half, const Settings &p_settings) {
	// The render buffers free everything when the internal size or the view
	// count changes; resolution and bent normal changes are checked here.
	if (p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_DEPTH)) {
		RD::TextureFormat tf = RD::get_singleton()->texture_get_format(p_render_buffers->get_texture(RB_SCOPE_XEGTAO, XEGTAO_DEPTH));
		bool has_bent = p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_BENT);
		if (int(tf.width) != p_ao_size.x || int(tf.height) != p_ao_size.y || has_bent != p_settings.bent_normals) {
			p_render_buffers->clear_context(RB_SCOPE_XEGTAO);
		}
	}

	const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	const Size2i full_size = p_render_buffers->get_internal_size();
	if (!p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_DEPTH)) {
		const RD::DataFormat ao_format = p_settings.bent_normals ? RD::DATA_FORMAT_R32_UINT : RD::DATA_FORMAT_R8_UNORM;
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_DEPTH, RD::DATA_FORMAT_R16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, p_ao_size, 0, XEGTAO_DEPTH_MIPS);
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_AO_A, ao_format, usage, RD::TEXTURE_SAMPLES_1, p_ao_size);
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_AO_B, ao_format, usage, RD::TEXTURE_SAMPLES_1, p_ao_size);
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_EDGES, RD::DATA_FORMAT_R8_UNORM, usage, RD::TEXTURE_SAMPLES_1, p_ao_size);
		if (p_half) {
			p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_HALF_VISIBILITY, RD::DATA_FORMAT_R8_UNORM, usage, RD::TEXTURE_SAMPLES_1, p_ao_size);
			if (p_settings.bent_normals) {
				p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_HALF_BENT, RD::DATA_FORMAT_R8G8B8A8_UNORM, usage, RD::TEXTURE_SAMPLES_1, p_ao_size);
			}
		}
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_VISIBILITY, RD::DATA_FORMAT_R8_UNORM, usage, RD::TEXTURE_SAMPLES_1, full_size);
		if (p_settings.bent_normals) {
			p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_BENT, RD::DATA_FORMAT_R8G8B8A8_UNORM, usage, RD::TEXTURE_SAMPLES_1, full_size);
		}
	}
	if (p_settings.debug_mode != 0 && !p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_DEBUG)) {
		p_render_buffers->create_texture(RB_SCOPE_XEGTAO, XEGTAO_DEBUG, RD::DATA_FORMAT_R8G8B8A8_UNORM, usage, RD::TEXTURE_SAMPLES_1, full_size);
	}
}

void XeGTAO::process(Ref<RenderSceneBuffersRD> p_render_buffers, const RID *p_normal_roughness, const Projection *p_projections, const Settings &p_settings) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	ERR_FAIL_COND(p_render_buffers.is_null());

	const Size2i full_size = p_render_buffers->get_internal_size();
	const Size2i ao_size = get_ao_size(full_size, p_settings);
	ERR_FAIL_COND(ao_size == Size2i());
	const bool half = ao_size != full_size;
	const bool bent = p_settings.bent_normals;
	const int quality = CLAMP(p_settings.quality, 0, 3);
	const int scale = half ? 2 : 1;

	_ensure_buffers(p_render_buffers, ao_size, half, p_settings);

	RD *rd = RD::get_singleton();
	PushConstant pc;
	memset(&pc, 0, sizeof(PushConstant));
	pc.size[0] = ao_size.x;
	pc.size[1] = ao_size.y;
	pc.pixel_size[0] = 1.0f / ao_size.x;
	pc.pixel_size[1] = 1.0f / ao_size.y;
	pc.full_size[0] = full_size.x;
	pc.full_size[1] = full_size.y;
	pc.scale = scale;
	pc.effect_radius = p_settings.radius * 1.457f; // XE_GTAO_DEFAULT_RADIUS_MULTIPLIER.
	pc.effect_falloff_range = 0.615f; // XE_GTAO_DEFAULT_FALLOFF_RANGE.
	// XE_GTAO_DEFAULT_FINAL_VALUE_POWER, scaled by the intensity: 0 removes
	// the occlusion, 2 squares the visibility.
	pc.final_value_power = 2.2f * p_settings.intensity;
	pc.denoise_blur_beta = 1.2f;
	// A fixed noise pattern without temporal accumulation: per-frame noise
	// would only show as flicker.
	pc.noise_index = p_settings.temporal ? uint32_t(p_settings.frame % 64) : 0;
	pc.debug_mode = uint32_t(p_settings.debug_mode);

	// One edge-aware pass is enough when TAA averages the frames (Intel's
	// "sharp"); without it a second one removes the remaining grain.
	const int denoise_passes = p_settings.temporal ? 1 : 2;

	for (uint32_t v = 0; v < p_render_buffers->get_view_count(); v++) {
		const Projection &proj = p_projections[v];

		// Screen uv (y down) to view xy, in XeGTAO's view space (y up), from
		// Godot's OpenGL style projection, including off-center frustums.
		const bool ortho = proj.is_orthogonal();
		float mul[2], add[2];
		mul[0] = 2.0f / proj.columns[0][0];
		mul[1] = -2.0f / proj.columns[1][1];
		if (ortho) {
			add[0] = (-1.0f - proj.columns[3][0]) / proj.columns[0][0];
			add[1] = (1.0f - proj.columns[3][1]) / proj.columns[1][1];
		} else {
			add[0] = (proj.columns[2][0] - 1.0f) / proj.columns[0][0];
			add[1] = (1.0f + proj.columns[2][1]) / proj.columns[1][1];
		}
		if (half) {
			// AO texel p stands for full resolution pixel 2p: map AO uv to
			// that pixel's uv.
			for (int i = 0; i < 2; i++) {
				float ratio = 2.0f * float(ao_size[i]) / float(full_size[i]);
				add[i] -= mul[i] * 0.5f / float(full_size[i]);
				mul[i] *= ratio;
			}
		}
		for (int i = 0; i < 2; i++) {
			pc.ndc_to_view_mul[i] = mul[i];
			pc.ndc_to_view_add[i] = add[i];
			pc.ndc_to_view_mul_x_pixel_size[i] = mul[i] * pc.pixel_size[i];
		}
		// XeGTAO uses the absolute pixel size.
		pc.ndc_to_view_mul_x_pixel_size[1] = Math::abs(pc.ndc_to_view_mul_x_pixel_size[1]);
		pc.orthogonal = ortho ? 1 : 0;

		// Raw depth (Vulkan clip space, reverse Z) to view depth.
		Projection correction;
		correction.set_depth_correction(false);
		Projection c = correction * proj;
		const float a = c.columns[2][2];
		const float b = c.columns[3][2];
		const float cw = c.columns[2][3];
		const float d = c.columns[3][3];
		pc.depth_unproject[0] = -d;
		pc.depth_unproject[1] = b;
		pc.depth_unproject[2] = cw;
		pc.depth_unproject[3] = -a;
		// Background: the far plane (raw depth 0), or the FP16 limit.
		const float far_depth = -b / (-a);
		pc.sky_depth = MIN(Math::abs(far_depth) * 0.999f, 65000.0f);

		RID depth_buffer = p_render_buffers->get_depth_texture(v);
		RID depth_mips = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_DEPTH, v, 0, 1, XEGTAO_DEPTH_MIPS);
		RID depth_mip0 = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_DEPTH, v, 0);
		RID ao_a = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_AO_A, v, 0);
		RID ao_b = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_AO_B, v, 0);
		RID edges = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_EDGES, v, 0);
		RID visibility = p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_VISIBILITY, v, 0);
		RID bent_normal = bent ? p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_BENT, v, 0) : RID();
		RID denoised_visibility = half ? p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_HALF_VISIBILITY, v, 0) : visibility;
		RID denoised_bent = (half && bent) ? p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_HALF_BENT, v, 0) : bent_normal;

		rd->draw_command_begin_label("XeGTAO");
		RD::ComputeListID compute_list;

		// Pass A: depth MIP chain.
		{
			RENDER_TIMESTAMP("XeGTAO Depth Preparation");
			compute_list = rd->compute_list_begin();
			RID shader = prefilter_shader.version_get_shader(prefilter_version, 0);
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ point_sampler, depth_buffer }));
			thread_local LocalVector<RD::Uniform> u_mips;
			u_mips.clear();
			for (int i = 0; i < XEGTAO_DEPTH_MIPS; i++) {
				RD::Uniform u(RD::UNIFORM_TYPE_IMAGE, i, Vector<RID>({ p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_DEPTH, v, i) }));
				u_mips.push_back(u);
			}
			rd->compute_list_bind_compute_pipeline(compute_list, prefilter_pipeline);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_depth), 0);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache_vec(shader, 1, u_mips), 1);
			rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
			// Each group covers 16x16 pixels.
			rd->compute_list_dispatch(compute_list, (ao_size.x + 15) / 16, (ao_size.y + 15) / 16, 1);
			rd->compute_list_end();
		}

		// Pass B: horizon search.
		{
			RENDER_TIMESTAMP("XeGTAO Main Pass");
			compute_list = rd->compute_list_begin();
			const int variant = quality + (bent ? 4 : 0);
			RID shader = main_shader.version_get_shader(main_version, variant);
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ point_sampler, depth_mips }));
			RD::Uniform u_normal(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ point_sampler, p_normal_roughness[v] }));
			RD::Uniform u_ao(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ ao_a }));
			RD::Uniform u_edges(RD::UNIFORM_TYPE_IMAGE, 1, Vector<RID>({ edges }));
			rd->compute_list_bind_compute_pipeline(compute_list, main_pipelines[variant]);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_depth, u_normal), 0);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_ao, u_edges), 1);
			rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
			rd->compute_list_dispatch_threads(compute_list, ao_size.x, ao_size.y, 1);
			rd->compute_list_end();
		}

		// Pass C: denoise. A -> B -> output, or A -> output; A keeps the raw
		// AO for the debug view.
		{
			RENDER_TIMESTAMP("XeGTAO Denoise");
			RID source = ao_a;
			for (int i = 0; i < denoise_passes; i++) {
				compute_list = rd->compute_list_begin();
				const bool final_pass = i == denoise_passes - 1;
				const int variant = (final_pass ? 1 : 0) + (bent ? 2 : 0);
				RID shader = denoise_shader.version_get_shader(denoise_version, variant);
				RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ point_sampler, source }));
				RD::Uniform u_edges(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ point_sampler, edges }));
				RID set1;
				if (final_pass) {
					RD::Uniform u_vis(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ denoised_visibility }));
					if (bent) {
						RD::Uniform u_bent(RD::UNIFORM_TYPE_IMAGE, 1, Vector<RID>({ denoised_bent }));
						set1 = uniform_set_cache->get_cache(shader, 1, u_vis, u_bent);
					} else {
						set1 = uniform_set_cache->get_cache(shader, 1, u_vis);
					}
				} else {
					RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ ao_b }));
					set1 = uniform_set_cache->get_cache(shader, 1, u_dest);
				}
				rd->compute_list_bind_compute_pipeline(compute_list, denoise_pipelines[variant]);
				rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_source, u_edges), 0);
				rd->compute_list_bind_uniform_set(compute_list, set1, 1);
				rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
				rd->compute_list_dispatch_threads(compute_list, ao_size.x, ao_size.y, 1);
				rd->compute_list_end();
				source = ao_b;
			}
		}

		// Pass D: half resolution only, depth-guided upscale.
		if (half) {
			RENDER_TIMESTAMP("XeGTAO Upscale");
			compute_list = rd->compute_list_begin();
			const int variant = bent ? 1 : 0;
			RID shader = upscale_shader.version_get_shader(upscale_version, variant);
			RD::Uniform u_vis(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ point_sampler, denoised_visibility }));
			RD::Uniform u_view_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ point_sampler, depth_mip0 }));
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ point_sampler, depth_buffer }));
			RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ visibility }));
			RID set0, set1;
			if (bent) {
				RD::Uniform u_bent(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ point_sampler, denoised_bent }));
				RD::Uniform u_out_bent(RD::UNIFORM_TYPE_IMAGE, 1, Vector<RID>({ bent_normal }));
				set0 = uniform_set_cache->get_cache(shader, 0, u_vis, u_view_depth, u_depth, u_bent);
				set1 = uniform_set_cache->get_cache(shader, 1, u_out, u_out_bent);
			} else {
				set0 = uniform_set_cache->get_cache(shader, 0, u_vis, u_view_depth, u_depth);
				set1 = uniform_set_cache->get_cache(shader, 1, u_out);
			}
			rd->compute_list_bind_compute_pipeline(compute_list, upscale_pipelines[variant]);
			rd->compute_list_bind_uniform_set(compute_list, set0, 0);
			rd->compute_list_bind_uniform_set(compute_list, set1, 1);
			rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
			rd->compute_list_dispatch_threads(compute_list, full_size.x, full_size.y, 1);
			rd->compute_list_end();
		}

		if (p_settings.debug_mode != 0) {
			compute_list = rd->compute_list_begin();
			const int variant = bent ? 1 : 0;
			RID shader = debug_shader.version_get_shader(debug_version, variant);
			RID black = RendererRD::TextureStorage::get_singleton()->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
			thread_local LocalVector<RD::Uniform> u_src;
			u_src.clear();
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ point_sampler, ao_a })));
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ point_sampler, edges })));
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ point_sampler, depth_mip0 })));
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ point_sampler, p_normal_roughness[v] })));
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ point_sampler, visibility })));
			u_src.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, Vector<RID>({ point_sampler, bent ? bent_normal : black })));
			RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_DEBUG, v, 0) }));
			rd->compute_list_bind_compute_pipeline(compute_list, debug_pipelines[variant]);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache_vec(shader, 0, u_src), 0);
			rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_out), 1);
			rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
			rd->compute_list_dispatch_threads(compute_list, full_size.x, full_size.y, 1);
			rd->compute_list_end();
		}

		RENDER_TIMESTAMP("XeGTAO Done");
		rd->draw_command_end_label();
	}
}

RID XeGTAO::get_visibility_texture(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers.is_null() || !p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_VISIBILITY)) {
		return RID();
	}
	return p_render_buffers->get_texture(RB_SCOPE_XEGTAO, XEGTAO_VISIBILITY);
}

RID XeGTAO::get_debug_texture(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers.is_null() || !p_render_buffers->has_texture(RB_SCOPE_XEGTAO, XEGTAO_DEBUG)) {
		return RID();
	}
	return p_render_buffers->get_texture_slice(RB_SCOPE_XEGTAO, XEGTAO_DEBUG, 0, 0);
}
