/**************************************************************************/
/*  mesh_clod.cpp                                                         */
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

#include "render_forward_clustered.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererSceneRenderImplementation;

void RenderForwardClustered::_prepare_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	if (!p_buffers) {
		return;
	}
	auto &buffers = p_buffers->mesh_clod;
	buffers.active = false;
#ifdef REAL_T_IS_DOUBLE
	return;
#endif
	const float error_pixels = GLOBAL_GET("rendering/geometry/cluster_lod/error_pixels");
	// The selection kernel always binds the statistics buffer; without it, keep source draws.
	if (p_buffers->geometry_statistics.buffer.is_null() || !bool(GLOBAL_GET("rendering/geometry/cluster_lod/enabled")) || !Math::is_finite(error_pixels) || error_pixels < 0 ||
			p_render_data->reflection_probe.is_valid() || p_render_data->scene_data->view_count != 1 ||
			p_render_data->scene_data->mirror_pass) {
		return;
	}
	struct Input {
		float planes[6][4];
		float camera_projection[4];
		float forward_near[4];
		uint32_t params[4];
	};
	static_assert(sizeof(Input) == 144);
	struct Work {
		RID groups;
		uint32_t group_count;
		RenderElementInfo *element;
		const RendererRD::MeshStorage::ClusterStreaming *streaming;
	};
	Vector<Input> inputs;
	Vector<Work> work;
	uint32_t total_groups = 0;
	const auto *scene = p_render_data->scene_data;
	// Jitter only widens the frustum; the error metric ignores the sub-pixel shift.
	const Vector<Plane> planes = _jitter_safe_projection(scene->cam_projection, scene->taa_jitter).get_projection_planes(scene->cam_transform);
	const Vector3 forward = -scene->cam_transform.basis.get_column(2).normalized();
	const float projection_scale = Math::abs(scene->cam_projection[1][1]) * p_render_data->render_buffers->get_internal_size().height * 0.5f;
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	// Streaming: apply earlier GPU feedback and refresh residency tables before
	// any selection of this frame reads them.
	storage->cluster_streaming_begin_frame();
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		GeometryInstanceForwardClustered *instance = surface->owner;
		RenderElementInfo &element = list.element_info[i];
		if (element.repeat != 1) {
			i += element.repeat - 1;
			continue;
		}
		if (instance->data->base_type != RSE::INSTANCE_MESH || instance->mesh_instance.is_valid() ||
				(instance->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES)) || element.lod_index != 0 ||
				(surface->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA) ||
				!instance->transform.is_finite() || Math::is_zero_approx(instance->transform.basis.determinant())) {
			continue;
		}
		const auto *shader = surface->shader;
		if (shader->uses_vertex || shader->uses_position || shader->uses_world_coordinates || shader->writes_modelview_or_projection || shader->uses_custom_vertex_transform ||
				shader->uses_z_clip_scale || shader->uses_point_size || shader->uses_alpha || shader->uses_blend_alpha || shader->uses_depth_prepass_alpha || shader->stencil_enabled) {
			continue;
		}
		const auto &resident = storage->mesh_surface_get_cluster_data(surface->surface);
		if (resident.group_count == 0 || total_groups + resident.group_count > 65536 || inputs.size() >= 4096) {
			continue;
		}
		// A streamed surface without all roots resident keeps source rendering.
		if (resident.streaming && (!resident.streaming->complete || resident.index_array.is_null())) {
			continue;
		}
		// All passes over a source surface must use the same geometry. Until
		// pass-chain eligibility is selected atomically, overlays/next_pass keep
		// source raster even if one individual material would be eligible.
		bool multiple_passes = false;
		for (const GeometryInstanceSurfaceDataCache *pass = instance->surface_caches; pass; pass = pass->next) {
			if (pass != surface && pass->surface == surface->surface) {
				multiple_passes = true;
				break;
			}
		}
		if (multiple_passes) {
			continue;
		}
		Input input = {};
		for (uint32_t p = 0; p < 6; p++) {
			const Vector3 normal = instance->transform.basis.transposed().xform(planes[p].normal);
			for (uint32_t axis = 0; axis < 3; axis++) {
				input.planes[p][axis] = normal[axis];
			}
			input.planes[p][3] = planes[p].d - planes[p].normal.dot(instance->transform.origin);
		}
		const Vector3 local_camera = instance->transform.affine_inverse().xform(scene->cam_transform.origin);
		const Vector3 local_forward = instance->transform.basis.transposed().xform(forward);
		// Frobenius norm bounds the spectral norm, including nonuniform scale/shear.
		const float error_scale = Math::sqrt(instance->transform.basis.get_column(0).length_squared() + instance->transform.basis.get_column(1).length_squared() + instance->transform.basis.get_column(2).length_squared());
		if (!local_camera.is_finite() || !local_forward.is_finite() || !Math::is_finite(projection_scale * error_scale)) {
			continue;
		}
		for (uint32_t axis = 0; axis < 3; axis++) {
			input.camera_projection[axis] = local_camera[axis];
			input.forward_near[axis] = local_forward[axis];
		}
		input.camera_projection[3] = projection_scale * error_scale;
		input.forward_near[3] = MAX(scene->z_near, 0.0001f);
		input.params[0] = resident.group_count;
		input.params[1] = total_groups * 5;
		input.params[2] = scene->cam_orthogonal;
		memcpy(&input.params[3], &error_pixels, sizeof(float));
		total_groups += resident.group_count;
		inputs.push_back(input);
		work.push_back({ resident.groups, resident.group_count, &element, resident.streaming });
	}
	if (work.is_empty()) {
		return;
	}
	RD *rd = RD::get_singleton();
	if (buffers.group_capacity < total_groups) {
		if (buffers.commands.is_valid()) {
			rd->free_rid(buffers.commands);
		}
		buffers.group_capacity = Math::next_power_of_2(total_groups);
		buffers.commands = rd->storage_buffer_create(buffers.group_capacity * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		if (buffers.commands.is_null()) {
			buffers.group_capacity = 0;
			return;
		}
	}
	if (buffers.draw_capacity < uint32_t(inputs.size())) {
		if (buffers.inputs.is_valid()) {
			rd->free_rid(buffers.inputs);
		}
		buffers.draw_capacity = Math::next_power_of_2(uint32_t(inputs.size()));
		buffers.inputs = rd->storage_buffer_create(buffers.draw_capacity * sizeof(Input));
		if (buffers.inputs.is_null()) {
			buffers.draw_capacity = 0;
			return;
		}
	}
	if (mesh_clod.version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		modes.push_back("\n#define MODE_RESIDENCY\n");
		mesh_clod.shader.initialize(modes, "\n#define GEOMETRY_STATISTICS\n");
		mesh_clod.version = mesh_clod.shader.version_create();
		for (uint32_t mode = 0; mode < 2; mode++) {
			mesh_clod.pipelines[mode] = rd->compute_pipeline_create(mesh_clod.shader.version_get_shader(mesh_clod.version, mode));
		}
	}
	for (RID pipeline : mesh_clod.pipelines) {
		if (pipeline.is_null()) {
			return;
		}
	}
	rd->buffer_update(buffers.inputs, 0, inputs.size() * sizeof(Input), inputs.ptr());
	p_buffers->geometry_statistics.clod_surfaces = work.size();
	rd->capture_timestamp("Mesh CLOD Selection");
	RD::ComputeListID compute_list = rd->compute_list_begin();
	uint32_t bound_mode = UINT32_MAX;
	for (uint32_t draw = 0; draw < work.size(); draw++) {
		const RendererRD::MeshStorage::ClusterStreaming *streaming = work[draw].streaming;
		const uint32_t mode = streaming ? 1 : 0;
		const RID shader = mesh_clod.shader.version_get_shader(mesh_clod.version, mode);
		if (mode != bound_mode) {
			rd->compute_list_bind_compute_pipeline(compute_list, mesh_clod.pipelines[mode]);
			bound_mode = mode;
		}
		LocalVector<RD::Uniform> uniforms;
		const RID resources[6] = { work[draw].groups, buffers.inputs, buffers.commands,
			streaming ? streaming->residency : RID(), streaming ? streaming->children_buffer : RID(), streaming ? storage->cluster_streaming_get_requests() : RID() };
		for (uint32_t binding = 0; binding < (streaming ? 6u : 3u); binding++) {
			RD::Uniform uniform;
			uniform.binding = binding;
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.append_id(resources[binding]);
			uniforms.push_back(uniform);
		}
		RD::Uniform statistics;
		statistics.binding = 6;
		statistics.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		statistics.append_id(p_buffers->geometry_statistics.buffer);
		uniforms.push_back(statistics);
		rd->compute_list_bind_uniform_set(compute_list, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		// The residency mode adds the surface's first word in the request buffer.
		const uint32_t push_constant[2] = { draw, streaming ? streaming->request_offset : 0 };
		rd->compute_list_set_push_constant(compute_list, push_constant, streaming ? sizeof(push_constant) : sizeof(uint32_t));
		rd->compute_list_dispatch(compute_list, (work[draw].group_count + 63) / 64, 1, 1);
		work[draw].element->mesh_clod_offset = inputs[draw].params[1];
		if (p_render_data->render_info) {
			p_render_data->render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += work[draw].group_count - 1;
		}
	}
	rd->compute_list_end();
	buffers.active = true;
}
