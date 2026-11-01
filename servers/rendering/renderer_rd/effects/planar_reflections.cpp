/**************************************************************************/
/*  planar_reflections.cpp                                                */
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

#include "planar_reflections.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

PlanarReflections::PlanarReflections() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_PROJECT\n");
	modes.push_back("\n#define MODE_RESOLVE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	params_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(Params));
}

PlanarReflections::~PlanarReflections() {
	RD::get_singleton()->free_rid(params_buffer);
	shader.version_free(shader_version);
}

int PlanarReflections::select_reflectors(const LocalVector<RendererSceneRender::RenderPlanarReflection> &p_reflectors, const Transform3D &p_cam_transform, const Projection &p_cam_projection, uint32_t p_visible_layers, int p_max, const RendererSceneRender::RenderPlanarReflection **r_selected) {
	p_max = CLAMP(p_max, 0, MAX_REFLECTORS);
	if (p_max == 0 || p_reflectors.is_empty()) {
		return 0;
	}

	Vector<Plane> frustum = p_cam_projection.get_projection_planes(p_cam_transform);
	Vector3 cam_pos = p_cam_transform.origin;
	Vector3 cam_forward = -p_cam_transform.basis.get_column(2).normalized();
	bool orthogonal = p_cam_projection.is_orthogonal();

	struct Candidate {
		const RendererSceneRender::RenderPlanarReflection *reflector = nullptr;
		float distance = 0.0;
		bool operator<(const Candidate &p_other) const { return distance < p_other.distance; }
	};
	LocalVector<Candidate> candidates;

	for (const RendererSceneRender::RenderPlanarReflection &r : p_reflectors) {
		if (r.intensity <= 0.0 || !(r.layers & p_visible_layers)) {
			continue;
		}
		const Basis &b = r.transform.basis;
		Vector3 normal = b.get_column(1).normalized();
		Vector3 origin = r.transform.origin;
		// Single sided: the camera must be on the reflected side.
		float side = orthogonal ? -cam_forward.dot(normal) : normal.dot(cam_pos - origin);
		if (side <= 0.0) {
			continue;
		}
		Vector3 hu = b.get_column(0) * (r.size.x * 0.5);
		Vector3 hv = b.get_column(2) * (r.size.y * 0.5);
		Vector3 corners[4] = { origin - hu - hv, origin + hu - hv, origin + hu + hv, origin - hu + hv };
		bool culled = false;
		for (const Plane &plane : frustum) {
			bool all_outside = true;
			for (int c = 0; c < 4; c++) {
				if (!plane.is_point_over(corners[c])) {
					all_outside = false;
					break;
				}
			}
			if (all_outside) {
				culled = true;
				break;
			}
		}
		if (culled) {
			continue;
		}
		Candidate candidate;
		candidate.reflector = &r;
		candidate.distance = MAX(cam_pos.distance_to(origin) - (hu + hv).length(), 0.0f);
		candidates.push_back(candidate);
	}

	candidates.sort();
	int count = MIN((int)candidates.size(), p_max);
	for (int i = 0; i < count; i++) {
		r_selected[i] = candidates[i].reflector;
	}
	return count;
}

void PlanarReflections::process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_depth, RID p_normal_roughness, RID p_last_frame, const Projection &p_projection, const Transform3D &p_cam_transform, const Projection &p_reprojection, const RendererSceneRender::RenderPlanarReflection *const *p_reflectors, int p_count, bool p_half_size) {
	ERR_FAIL_COND(p_render_buffers.is_null());
	ERR_FAIL_COND(p_count <= 0 || p_count > MAX_REFLECTORS);
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	Size2i screen_size = p_render_buffers->get_internal_size();
	Size2i target_size = p_half_size ? Size2i((screen_size.x + 1) / 2, (screen_size.y + 1) / 2) : screen_size;

	// Buffers: recreated when the size changes.
	if (p_render_buffers->has_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_COLOR)) {
		RD::TextureFormat format = p_render_buffers->get_texture_format(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_COLOR);
		if ((int)format.width != target_size.x || (int)format.height != target_size.y) {
			p_render_buffers->clear_context(RB_SCOPE_PLANAR_REFLECTIONS);
		}
	}
	if (!p_render_buffers->has_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_COLOR)) {
		p_render_buffers->create_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_INTERMEDIATE, RD::DATA_FORMAT_R32_UINT, RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_STORAGE_ATOMIC_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT, RD::TEXTURE_SAMPLES_1, target_size, 1, 1);
		p_render_buffers->create_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_COLOR, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1, target_size, 1, 1);
		p_render_buffers->create_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_PARAMS, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1, target_size, 1, 1);
	}
	RID intermediate = p_render_buffers->get_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_INTERMEDIATE);
	RID color = p_render_buffers->get_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_COLOR);
	RID params_texture = p_render_buffers->get_texture(RB_SCOPE_PLANAR_REFLECTIONS, RB_PLANAR_PARAMS);

	// Parameters, reflectors in view space.
	Params params = {};
	Projection correction;
	correction.set_depth_correction(true);
	Projection projection = correction * p_projection;
	MaterialStorage::store_camera(projection, params.projection);
	MaterialStorage::store_camera(projection.inverse(), params.inv_projection);
	MaterialStorage::store_camera(p_reprojection, params.reprojection);
	params.screen_size[0] = screen_size.x;
	params.screen_size[1] = screen_size.y;
	params.target_size[0] = target_size.x;
	params.target_size[1] = target_size.y;
	params.reflector_count = p_count;
	params.max_color_lod = MAX(float(RD::get_singleton()->texture_get_format(p_last_frame).mipmaps) - 1.0f, 0.0f);
	params.orthogonal = p_projection.is_orthogonal() ? 1 : 0;

	Transform3D to_view = p_cam_transform.affine_inverse();
	for (int i = 0; i < p_count; i++) {
		const RendererSceneRender::RenderPlanarReflection &r = *p_reflectors[i];
		const Basis &b = r.transform.basis;
		Vector3 normal = to_view.basis.xform(b.get_column(1)).normalized();
		Vector3 center = to_view.xform(r.transform.origin);
		Vector3 u = to_view.basis.xform(b.get_column(0));
		Vector3 v = to_view.basis.xform(b.get_column(2));
		float half_u = r.size.x * 0.5 * u.length();
		float half_v = r.size.y * 0.5 * v.length();
		u.normalize();
		v.normalize();

		ReflectorData &d = params.reflectors[i];
		d.plane[0] = normal.x;
		d.plane[1] = normal.y;
		d.plane[2] = normal.z;
		d.plane[3] = normal.dot(center);
		d.center[0] = center.x;
		d.center[1] = center.y;
		d.center[2] = center.z;
		d.center[3] = r.intensity;
		d.axis_u[0] = u.x;
		d.axis_u[1] = u.y;
		d.axis_u[2] = u.z;
		d.axis_u[3] = half_u;
		d.axis_v[0] = v.x;
		d.axis_v[1] = v.y;
		d.axis_v[2] = v.z;
		d.axis_v[3] = half_v;
		d.params[0] = r.thickness;
		d.params[1] = r.normal_distortion;
		d.params[2] = r.roughness_cutoff;
		d.params[3] = 0.0;
	}
	RD::get_singleton()->buffer_update(params_buffer, 0, sizeof(Params), &params);

	RD::get_singleton()->draw_command_begin_label("Planar Reflections");

	RD::get_singleton()->texture_clear(intermediate, Color(0, 0, 0, 0), 0, 1, 0, 1);

	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID linear_mips = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, params_buffer);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_depth }));
	RD::Uniform u_intermediate(RD::UNIFORM_TYPE_IMAGE, 2, intermediate);
	RD::Uniform u_normal_roughness(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, p_normal_roughness }));
	RD::Uniform u_last_frame(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ linear_mips, p_last_frame }));
	RD::Uniform u_color(RD::UNIFORM_TYPE_IMAGE, 5, color);
	RD::Uniform u_params_out(RD::UNIFORM_TYPE_IMAGE, 6, params_texture);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	for (int mode = 0; mode < MODE_MAX; mode++) {
		RID shader_rid = shader.version_get_shader(shader_version, mode);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[mode]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rid, 0, u_params, u_depth, u_intermediate, u_normal_roughness, u_last_frame, u_color, u_params_out), 0);
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, target_size.x, target_size.y, 1);
		if (mode == MODE_PROJECT) {
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}
	}
	RD::get_singleton()->compute_list_end();

	RD::get_singleton()->draw_command_end_label();
}
