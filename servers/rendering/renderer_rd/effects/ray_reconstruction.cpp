/**************************************************************************/
/*  ray_reconstruction.cpp                                                */
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

#include "ray_reconstruction.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_default.h" // IWYU pragma: keep. RENDER_TIMESTAMP macro uses RSG.

using namespace RendererRD;

#define RB_SCOPE_RR_HISTORY SNAME("native_rr_history")

// A-trous iterations (step sizes 1, 2, 4, 8, 16): a 3x3 kernel reaches 63 x 63 pixels.
static constexpr int ATROUS_ITERATIONS = 5;

RayReconstruction::RayReconstruction() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_TEMPORAL\n");
	modes.push_back("\n#define MODE_VARIANCE\n");
	modes.push_back("\n#define MODE_ATROUS\n");
	modes.push_back("\n#define MODE_REFERENCE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

RayReconstruction::~RayReconstruction() {
	for (KeyValue<RenderSceneBuffersRD *, ViewportState> &E : viewports) {
		if (E.value.params_buffer.is_valid()) {
			RD::get_singleton()->free_rid(E.value.params_buffer);
		}
	}
	shader.version_free(shader_version);
}

RID RayReconstruction::_get_shader(Mode p_mode) {
	return shader.version_get_shader(shader_version, p_mode);
}

void RayReconstruction::free_viewport(RenderSceneBuffersRD *p_render_buffers) {
	HashMap<RenderSceneBuffersRD *, ViewportState>::Iterator it = viewports.find(p_render_buffers);
	if (it == viewports.end()) {
		return;
	}
	if (it->value.params_buffer.is_valid()) {
		RD::get_singleton()->free_rid(it->value.params_buffer);
	}
	viewports.remove(it);
}

// Creates the history and filter textures. Returns false when they were just
// created (no usable history yet).
bool RayReconstruction::_ensure_history(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers->has_texture(RB_SCOPE_RR_HISTORY, SNAME("surface_0"))) {
		return true;
	}
	const uint32_t usage = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	for (int i = 0; i < 2; i++) {
		const String n = itos(i);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("diffuse_" + n), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("specular_" + n), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("moments_" + n), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("surface_" + n), RD::DATA_FORMAT_R32G32_UINT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("specular_hit_" + n), RD::DATA_FORMAT_R16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("filter_diffuse_" + n), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, StringName("filter_specular_" + n), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
	}
	return false;
}

// Debug reference: a plain running average of the path tracer's image (no
// filtering), restarted whenever the camera moves or the mode is entered. Test
// harnesses freeze their animation and wait for it to converge.
void RayReconstruction::_process_reference(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs, ViewportState &r_state) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RD *rd = RD::get_singleton();

	if (!p_render_buffers->has_texture(RB_SCOPE_RR_HISTORY, SNAME("reference"))) {
		p_render_buffers->create_texture(RB_SCOPE_RR_HISTORY, SNAME("reference"), RD::DATA_FORMAT_R32G32B32A32_SFLOAT, RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1, Size2i(), 1);
		r_state.reference_active = false;
	}
	const bool reset = !r_state.reference_active || !r_state.reference_cam_transform.is_equal_approx(p_inputs.cam_transform);
	r_state.reference_active = true;
	r_state.reference_cam_transform = p_inputs.cam_transform;
	// The denoiser's history is stale once this mode ends.
	r_state.frame = 0;

	PushConstant pc = {};
	pc.flags = reset ? FLAG_RESET : 0;
	pc.debug_mode = DEBUG_REFERENCE;

	RENDER_TIMESTAMP("RR Reference");
	RID s = _get_shader(MODE_REFERENCE);
	RD::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipelines[MODE_REFERENCE]);
	rd->compute_list_bind_uniform_set(cl, uniform_set_cache->get_cache(s, 0, RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, r_state.params_buffer)), 0);
	RID set = uniform_set_cache->get_cache(s, 1,
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_inputs.base),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_inputs.diffuse),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, p_inputs.specular),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, p_render_buffers->get_texture(RB_SCOPE_RR_HISTORY, SNAME("reference"))),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, p_inputs.output));
	rd->compute_list_bind_uniform_set(cl, set, 1);
	rd->compute_list_set_push_constant(cl, &pc, sizeof(PushConstant));
	rd->compute_list_dispatch_threads(cl, p_inputs.size.x, p_inputs.size.y, 1);
	rd->compute_list_end();
	RENDER_TIMESTAMP("RR Done");
}

void RayReconstruction::process(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	RD *rd = RD::get_singleton();

	ViewportState &vs = viewports[p_render_buffers.ptr()];
	if (vs.params_buffer.is_null()) {
		vs.params_buffer = rd->uniform_buffer_create(sizeof(ParamsUBO));
	}

	const uint32_t debug_mode = uint32_t(int(GLOBAL_GET_CACHED(int, "rendering/ray_reconstruction/debug_mode")));
	if (debug_mode == DEBUG_REFERENCE) {
		ParamsUBO ubo = {};
		ubo.size[0] = p_inputs.size.x;
		ubo.size[1] = p_inputs.size.y;
		rd->buffer_update(vs.params_buffer, 0, sizeof(ParamsUBO), &ubo);
		_process_reference(p_render_buffers, p_inputs, vs);
		return;
	}
	vs.reference_active = false;
	const bool history_valid = _ensure_history(p_render_buffers) && vs.frame > 0;
	const uint32_t cur = vs.frame & 1;
	const uint32_t prev = cur ^ 1;
	vs.frame++;

	auto tex = [&](const char *p_name, uint32_t p_index) -> RID {
		return p_render_buffers->get_texture(RB_SCOPE_RR_HISTORY, StringName(String(p_name) + itos(p_index)));
	};

	ParamsUBO ubo = {};
	MaterialStorage::store_camera(p_inputs.projection.inverse(), ubo.inv_projection);
	MaterialStorage::store_transform(Transform3D(p_inputs.cam_transform.basis, Vector3()), ubo.view_to_world_rotation);
	MaterialStorage::store_transform(p_inputs.prev_cam_transform.affine_inverse() * p_inputs.cam_transform, ubo.current_to_previous_view);
	ubo.size[0] = p_inputs.size.x;
	ubo.size[1] = p_inputs.size.y;
	ubo.size[2] = 1.0f / p_inputs.size.x;
	ubo.size[3] = 1.0f / p_inputs.size.y;
	ubo.history[0] = 32.0f; // Max diffuse history (frames).
	ubo.history[1] = 24.0f; // Max specular history (rough surfaces).
	ubo.history[2] = 8.0f; // Max moment history.
	ubo.history[3] = history_valid ? 1.0f : 0.0f;
	ubo.filter_params[0] = 4.0f; // Luminance sigma (in standard deviations).
	ubo.filter_params[1] = 128.0f; // Normal power.
	ubo.filter_params[2] = 1.0f; // Plane distance sigma (in pixel footprints).
	ubo.filter_params[3] = 2.0f / (Math::abs(p_inputs.projection.columns[1][1]) * p_inputs.size.y); // Pixel size at depth 1.
	// View ray at unit depth, affine in uv: xy = scale * uv + offset.
	auto store_view_ray = [](const Projection &p_projection, float *r_out) {
		const Projection inv_projection = p_projection.inverse();
		auto ray = [&](real_t p_u, real_t p_v) -> Vector2 {
			Vector4 r = inv_projection.xform(Vector4(p_u * 2.0 - 1.0, p_v * 2.0 - 1.0, 1.0, 1.0));
			Vector3 d = Vector3(r.x, r.y, r.z) / r.w;
			return Vector2(d.x, d.y) / -d.z;
		};
		const Vector2 o = ray(0.0, 0.0);
		r_out[0] = ray(1.0, 0.0).x - o.x;
		r_out[1] = ray(0.0, 1.0).y - o.y;
		r_out[2] = o.x;
		r_out[3] = o.y;
	};
	store_view_ray(p_inputs.projection, ubo.view_ray);
	store_view_ray(p_inputs.prev_projection, ubo.previous_view_ray);
	MaterialStorage::store_camera(p_inputs.projection_unjittered, ubo.projection_unjittered);
	MaterialStorage::store_camera(p_inputs.prev_projection_unjittered, ubo.previous_projection_unjittered);
	MaterialStorage::store_transform(p_inputs.cam_transform.affine_inverse() * p_inputs.prev_cam_transform, ubo.previous_to_current_view);
	ubo.specular_params[0] = 0.4f; // Virtual (reflection) reprojection blends to surface motion up to this roughness.
	rd->buffer_update(vs.params_buffer, 0, sizeof(ParamsUBO), &ubo);

	PushConstant pc = {};
	pc.debug_mode = debug_mode;

	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vs.params_buffer);

	rd->draw_command_begin_label("Ray Reconstruction");

	// 1. Temporal accumulation.
	RENDER_TIMESTAMP("RR Temporal");
	const RID linear_sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	{
		RID s = _get_shader(MODE_TEMPORAL);
		RD::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, pipelines[MODE_TEMPORAL]);
		rd->compute_list_bind_uniform_set(cl, uniform_set_cache->get_cache(s, 0, u_params), 0);
		RID set = uniform_set_cache->get_cache(s, 1,
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_inputs.diffuse),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_inputs.specular),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, p_inputs.guide),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, p_inputs.depth),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, p_inputs.velocity),
				RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, Vector<RID>({ linear_sampler, tex("diffuse_", prev) })),
				RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 6, Vector<RID>({ linear_sampler, tex("specular_", prev) })),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 7, tex("moments_", prev)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 8, tex("surface_", prev)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 9, tex("diffuse_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 10, tex("specular_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 11, tex("moments_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 12, tex("surface_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 13, tex("specular_hit_", prev)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 14, tex("specular_hit_", cur)));
		rd->compute_list_bind_uniform_set(cl, set, 1);
		rd->compute_list_set_push_constant(cl, &pc, sizeof(PushConstant));
		rd->compute_list_dispatch_threads(cl, p_inputs.size.x, p_inputs.size.y, 1);
		rd->compute_list_end();
	}

	// 2. Variance (spatial estimate where the history is short).
	RENDER_TIMESTAMP("RR Variance");
	{
		RID s = _get_shader(MODE_VARIANCE);
		RD::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, pipelines[MODE_VARIANCE]);
		rd->compute_list_bind_uniform_set(cl, uniform_set_cache->get_cache(s, 0, u_params), 0);
		RID set = uniform_set_cache->get_cache(s, 1,
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, tex("surface_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, tex("diffuse_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, tex("specular_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, tex("moments_", cur)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, tex("filter_diffuse_", 0)),
				RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, tex("filter_specular_", 0)));
		rd->compute_list_bind_uniform_set(cl, set, 1);
		rd->compute_list_set_push_constant(cl, &pc, sizeof(PushConstant));
		rd->compute_list_dispatch_threads(cl, p_inputs.size.x, p_inputs.size.y, 1);
		rd->compute_list_end();
	}

	// 3. Edge-aware a-trous iterations; the last one composes the final image.
	{
		RID s = _get_shader(MODE_ATROUS);
		for (int i = 0; i < ATROUS_ITERATIONS; i++) {
			// One compute list per iteration, so each has its own timestamp.
			RENDER_TIMESTAMP(vformat("RR Filter %d", i));
			const uint32_t src = i & 1;
			const uint32_t dst = src ^ 1;
			RD::ComputeListID cl = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(cl, pipelines[MODE_ATROUS]);
			rd->compute_list_bind_uniform_set(cl, uniform_set_cache->get_cache(s, 0, u_params), 0);
			RID set = uniform_set_cache->get_cache(s, 1,
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, tex("surface_", cur)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, tex("filter_diffuse_", src)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, tex("filter_specular_", src)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, tex("filter_diffuse_", dst)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, tex("filter_specular_", dst)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 5, tex("diffuse_", cur)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 6, tex("specular_", cur)),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 7, p_inputs.base),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 8, p_inputs.guide),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 9, p_inputs.diffuse),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 10, p_inputs.specular),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 11, p_inputs.output),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 12, tex("specular_hit_", cur)));
			rd->compute_list_bind_uniform_set(cl, set, 1);
			pc.step_size = 1 << i;
			pc.iteration = i;
			// No feedback of filtered results into the history (SVGF does it):
			// measured, it darkened the image (the filter's bias adds up every
			// frame) without lowering the error.
			pc.flags = i == ATROUS_ITERATIONS - 1 ? FLAG_COMPOSE : 0;
			rd->compute_list_set_push_constant(cl, &pc, sizeof(PushConstant));
			rd->compute_list_dispatch_threads(cl, p_inputs.size.x, p_inputs.size.y, 1);
			rd->compute_list_end();
		}
	}

	RENDER_TIMESTAMP("RR Done");
	rd->draw_command_end_label();
}
