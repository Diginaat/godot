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

bool RenderForwardClustered::_mesh_clod_frame_enabled(const RenderDataRD *p_render_data, const RenderBufferDataForwardClustered *p_buffers) const {
#ifdef REAL_T_IS_DOUBLE
	return false;
#else
	if (!p_buffers) {
		return false;
	}
	const float error_pixels = GLOBAL_GET_CACHED(float, "rendering/geometry/cluster_lod/error_pixels");
	return GLOBAL_GET_CACHED(bool, "rendering/geometry/cluster_lod/enabled") && Math::is_finite(error_pixels) && error_pixels >= 0 &&
			p_render_data->reflection_probe.is_null() && p_render_data->scene_data->view_count == 1 && !p_render_data->scene_data->mirror_pass;
#endif
}

bool RenderForwardClustered::_mesh_clod_surface_eligible(const GeometryInstanceSurfaceDataCache *p_surface) const {
	// Most surfaces have no cluster data: test that first, it runs for every
	// opaque element while the list is filled.
	const auto &resident = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_cluster_data(p_surface->surface);
	if (resident.group_count == 0) {
		return false;
	}
	const GeometryInstanceForwardClustered *instance = p_surface->owner;
	if (instance->data->base_type != RSE::INSTANCE_MESH || !instance->data->use_cluster_lod || instance->mesh_instance.is_valid() ||
			(instance->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES)) || p_surface->sort.lod_index != 0 ||
			(p_surface->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA) ||
			!instance->transform.is_finite() || Math::is_zero_approx(instance->transform.basis.determinant())) {
		return false;
	}
	const auto *shader = p_surface->shader;
	if (shader->uses_vertex || shader->uses_position || shader->uses_world_coordinates || shader->writes_modelview_or_projection || shader->uses_custom_vertex_transform ||
			shader->uses_z_clip_scale || shader->uses_point_size || shader->uses_alpha || shader->uses_blend_alpha || shader->uses_depth_prepass_alpha || shader->stencil_enabled) {
		return false;
	}
	// A streamed surface without all roots resident keeps source rendering.
	if (resident.streaming && (!resident.streaming->complete || resident.index_array.is_null())) {
		return false;
	}
	// All passes over a source surface must use the same geometry. Until
	// pass-chain eligibility is selected atomically, overlays/next_pass keep
	// source raster even if one individual material would be eligible.
	for (const GeometryInstanceSurfaceDataCache *pass = instance->surface_caches; pass; pass = pass->next) {
		if (pass != p_surface && pass->surface == p_surface->surface) {
			return false;
		}
	}
	return true;
}

void RenderForwardClustered::_set_mesh_clod_stream(RenderListParameters &r_params, const RenderBufferDataForwardClustered *p_buffers, uint32_t p_stream) {
	r_params.mesh_clod_commands = RID();
	r_params.mesh_clod_counts = RID();
	r_params.mesh_clod_count_offset = 0;
	if (!p_buffers || !p_buffers->mesh_clod.active) {
		return;
	}
	const auto &buffers = p_buffers->mesh_clod;
	using Buffers = RenderBufferDataForwardClustered::MeshCLODBuffers;
	ERR_FAIL_UNSIGNED_INDEX(p_stream, uint32_t(Buffers::STREAM_MAX));
	const RID streams[Buffers::STREAM_MAX] = { buffers.commands, buffers.color_commands, buffers.depth_commands, buffers.phase2_commands };
	r_params.mesh_clod_commands = streams[p_stream];
	if (buffers.compact && streams[p_stream].is_valid()) {
		r_params.mesh_clod_counts = buffers.counts;
		r_params.mesh_clod_count_offset = p_stream * buffers.draw_capacity * sizeof(uint32_t);
	}
}

bool RenderForwardClustered::_mesh_clod_make_input(const Transform3D &p_transform, const MeshCLODView &p_view, uint32_t p_group_count, uint32_t p_command_offset, MeshCLODInput &r_input) const {
	r_input = {};
	const Transform3D &transform = p_transform;
	for (uint32_t p = 0; p < 6; p++) {
		const Vector3 normal = transform.basis.transposed().xform(p_view.planes[p].normal);
		for (uint32_t axis = 0; axis < 3; axis++) {
			r_input.planes[p][axis] = normal[axis];
		}
		r_input.planes[p][3] = p_view.planes[p].d - p_view.planes[p].normal.dot(transform.origin);
	}
	const Vector3 local_eye = transform.affine_inverse().xform(p_view.eye);
	const Vector3 local_forward = transform.basis.transposed().xform(p_view.forward);
	// Frobenius norm bounds the spectral norm, including nonuniform scale/shear.
	const float error_scale = Math::sqrt(transform.basis.get_column(0).length_squared() + transform.basis.get_column(1).length_squared() + transform.basis.get_column(2).length_squared());
	if (!local_eye.is_finite() || !local_forward.is_finite() || !Math::is_finite(p_view.projection_scale * error_scale)) {
		return false;
	}
	for (uint32_t axis = 0; axis < 3; axis++) {
		r_input.camera_projection[axis] = local_eye[axis];
		r_input.forward_near[axis] = local_forward[axis];
	}
	r_input.camera_projection[3] = p_view.projection_scale * error_scale;
	r_input.forward_near[3] = MAX(p_view.z_near, 0.0001f);
	r_input.params[0] = p_group_count;
	r_input.params[1] = p_command_offset;
	r_input.params[2] = p_view.orthogonal;
	memcpy(&r_input.params[3], &p_view.error_pixels, sizeof(float));
	return true;
}

bool RenderForwardClustered::_mesh_clod_pipelines() {
	if (mesh_clod.version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		modes.push_back("\n#define MODE_RESIDENCY\n");
		mesh_clod.shader.initialize(modes, "\n#define GEOMETRY_STATISTICS\n");
		mesh_clod.version = mesh_clod.shader.version_create();
		for (uint32_t mode = 0; mode < 2; mode++) {
			mesh_clod.pipelines[mode] = RD::get_singleton()->compute_pipeline_create(mesh_clod.shader.version_get_shader(mesh_clod.version, mode));
		}
	}
	for (RID pipeline : mesh_clod.pipelines) {
		if (pipeline.is_null()) {
			return false;
		}
	}
	return true;
}

void RenderForwardClustered::_mesh_clod_dispatch(const LocalVector<MeshCLODWork> &p_work, RID p_inputs, RID p_commands, RID p_selection, RID p_counts, RID p_statistics, bool p_compact) {
	RD *rd = RD::get_singleton();
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	RD::ComputeListID compute_list = rd->compute_list_begin();
	uint32_t bound_mode = UINT32_MAX;
	RID bound_set;
	for (uint32_t draw = 0; draw < p_work.size(); draw++) {
		const RendererRD::MeshStorage::ClusterStreaming *streaming = p_work[draw].streaming;
		const uint32_t mode = streaming ? 1 : 0;
		const RID shader = mesh_clod.shader.version_get_shader(mesh_clod.version, mode);
		if (mode != bound_mode) {
			rd->compute_list_bind_compute_pipeline(compute_list, mesh_clod.pipelines[mode]);
			bound_mode = mode;
			bound_set = RID();
		}
		LocalVector<RD::Uniform> uniforms;
		const RID resources[6] = { p_work[draw].groups, p_inputs, p_commands,
			streaming ? streaming->residency : RID(), streaming ? streaming->children_buffer : RID(), streaming ? storage->cluster_streaming_get_requests() : RID() };
		for (uint32_t binding = 0; binding < (streaming ? 6u : 3u); binding++) {
			RD::Uniform uniform;
			uniform.binding = binding;
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.append_id(resources[binding]);
			uniforms.push_back(uniform);
		}
		for (const RD::Uniform &uniform : { RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 7, p_selection), RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, p_counts), RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, p_statistics) }) {
			uniforms.push_back(uniform);
		}
		// Instances of one surface share the set; only the push constant changes.
		const RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms);
		if (set != bound_set) {
			rd->compute_list_bind_uniform_set(compute_list, set, 0);
			bound_set = set;
		}
		// The residency mode adds the surface's first word in the request buffer.
		const uint32_t push_constant[3] = { draw, p_compact ? 1u : 0u, streaming ? streaming->request_offset : 0 };
		rd->compute_list_set_push_constant(compute_list, push_constant, streaming ? sizeof(push_constant) : 2 * sizeof(uint32_t));
		rd->compute_list_dispatch(compute_list, (p_work[draw].group_count + 63) / 64, 1, 1);
	}
	rd->compute_list_end();
}

void RenderForwardClustered::_prepare_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	if (!p_buffers) {
		return;
	}
	auto &buffers = p_buffers->mesh_clod;
	buffers.active = false;
	// Set again by _setup_mesh_clod_occlusion() when this frame's depth pre-pass tests the groups.
	buffers.occlusion_ready = false;
	buffers.two_pass = false;
	// The selection kernel always binds the statistics buffer; without it, keep source draws.
	if (p_buffers->geometry_statistics.buffer.is_null() || !_mesh_clod_frame_enabled(p_render_data, p_buffers)) {
		return;
	}
	static_assert(sizeof(MeshCLODInput) == 144);
	LocalVector<MeshCLODInput> inputs;
	LocalVector<MeshCLODWork> work;
	LocalVector<RenderElementInfo *> elements;
	uint32_t total_groups = 0;
	const auto *scene = p_render_data->scene_data;
	MeshCLODView view;
	// Jitter only widens the frustum; the error metric ignores the sub-pixel shift.
	view.planes = _jitter_safe_projection(scene->cam_projection, scene->taa_jitter).get_projection_planes(scene->cam_transform);
	view.eye = scene->cam_transform.origin;
	view.forward = -scene->cam_transform.basis.get_column(2).normalized();
	view.projection_scale = Math::abs(scene->cam_projection[1][1]) * p_render_data->render_buffers->get_internal_size().height * 0.5f;
	view.z_near = scene->z_near;
	view.orthogonal = scene->cam_orthogonal;
	view.error_pixels = GLOBAL_GET_CACHED(float, "rendering/geometry/cluster_lod/error_pixels");
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	// Streaming: apply earlier GPU feedback and refresh residency tables before
	// any selection of this frame reads them.
	storage->cluster_streaming_begin_frame();
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		RenderElementInfo &element = list.element_info[i];
		// _fill_instance_data() does not batch eligible surfaces, so a
		// repeated element is never eligible.
		if (element.repeat != 1) {
			i += element.repeat - 1;
			continue;
		}
		if (!_mesh_clod_surface_eligible(surface)) {
			continue;
		}
		const auto &resident = storage->mesh_surface_get_cluster_data(surface->surface);
		// Viewport limits; the buffers grow only to what a frame uses (20 bytes
		// of commands per group, 144 bytes of input per draw).
		if (total_groups + resident.group_count > (1u << 20) || inputs.size() >= 16384) {
			continue;
		}
		MeshCLODInput input;
		if (!_mesh_clod_make_input(surface->owner->transform, view, resident.group_count, total_groups * 5, input)) {
			continue;
		}
		total_groups += resident.group_count;
		inputs.push_back(input);
		work.push_back({ resident.groups, resident.group_count, resident.streaming });
		elements.push_back(&element);
	}
	if (work.is_empty()) {
		return;
	}
	RD *rd = RD::get_singleton();
	if (buffers.group_capacity < total_groups) {
		for (RID *buffer : { &buffers.commands, &buffers.selection }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
		}
		buffers.group_capacity = Math::next_power_of_2(total_groups);
		buffers.commands = rd->storage_buffer_create(buffers.group_capacity * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		buffers.selection = rd->storage_buffer_create(buffers.group_capacity * sizeof(uint32_t));
		if (buffers.commands.is_null() || buffers.selection.is_null()) {
			buffers.group_capacity = 0;
			return;
		}
	}
	if (buffers.draw_capacity < inputs.size()) {
		for (RID *buffer : { &buffers.inputs, &buffers.counts }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
		}
		buffers.draw_capacity = Math::next_power_of_2(inputs.size());
		buffers.inputs = rd->storage_buffer_create(buffers.draw_capacity * sizeof(MeshCLODInput));
		buffers.counts = rd->storage_buffer_create(buffers.draw_capacity * RenderBufferDataForwardClustered::MeshCLODBuffers::STREAM_MAX * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		if (buffers.inputs.is_null() || buffers.counts.is_null()) {
			buffers.draw_capacity = 0;
			return;
		}
	}
	// Compact streams: the kernels append only the groups they draw and count
	// them, and every pass draws with the GPU count, so the command processor
	// never reads the empty records of the groups outside the cut (most of
	// them). The Cluster LOD debug view pushes each group's id per record, so
	// it keeps one record per group.
	buffers.compact = mesh_clod.multi_draw && mesh_clod.indirect_count && get_debug_draw_mode() != RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_LOD &&
			GLOBAL_GET_CACHED(bool, "rendering/geometry/cluster_lod/compact_commands");
	if (buffers.compact) {
		rd->buffer_clear(buffers.counts, 0, buffers.draw_capacity * RenderBufferDataForwardClustered::MeshCLODBuffers::STREAM_MAX * sizeof(uint32_t));
	}
	if (!_mesh_clod_pipelines()) {
		return;
	}
	rd->buffer_update(buffers.inputs, 0, inputs.size() * sizeof(MeshCLODInput), inputs.ptr());
	p_buffers->geometry_statistics.clod_surfaces = work.size();
	rd->capture_timestamp("Mesh CLOD Selection");
	_mesh_clod_dispatch(work, buffers.inputs, buffers.commands, buffers.selection, buffers.counts, p_buffers->geometry_statistics.buffer, buffers.compact);
	for (uint32_t draw = 0; draw < work.size(); draw++) {
		elements[draw]->mesh_clod_offset = inputs[draw].params[1];
		elements[draw]->mesh_clod_draw = draw;
		if (p_render_data->render_info) {
			p_render_data->render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += work[draw].group_count - 1;
		}
	}
	buffers.active = true;
}

bool RenderForwardClustered::_mesh_clod_shadows_enabled() const {
#ifdef REAL_T_IS_DOUBLE
	return false;
#else
	const float error_texels = GLOBAL_GET_CACHED(float, "rendering/geometry/cluster_lod/shadow_error_texels");
	return GLOBAL_GET_CACHED(bool, "rendering/geometry/cluster_lod/enabled") && GLOBAL_GET_CACHED(bool, "rendering/geometry/cluster_lod/shadows") &&
			Math::is_finite(error_texels) && error_texels >= 0;
#endif
}

void RenderForwardClustered::_mesh_clod_shadow_append(uint32_t p_from, uint32_t p_count, const Projection &p_projection, const Transform3D &p_transform, uint32_t p_height) {
	MeshCLOD::Shadow &shadow = mesh_clod.shadow;
	MeshCLODView view;
	view.planes = p_projection.get_projection_planes(p_transform);
	ERR_FAIL_COND(view.planes.size() != 6);
	// No near plane: directional casters between the light and the cascade are
	// pancaked onto it, and nothing a near plane removes would draw anyway.
	view.planes.write[Projection::PLANE_NEAR] = Plane(Vector3(), 1e20f);
	view.eye = p_transform.origin;
	view.forward = -p_transform.basis.get_column(2).normalized();
	// Texels per world unit (orthographic cascades) or per unit at distance 1.
	view.projection_scale = Math::abs(p_projection.columns[1][1]) * float(p_height) * 0.5f;
	view.orthogonal = p_projection.is_orthogonal();
	view.z_near = view.orthogonal ? 0.0001f : p_projection.get_z_near();
	view.error_pixels = GLOBAL_GET_CACHED(float, "rendering/geometry/cluster_lod/shadow_error_texels");
	if (!Math::is_finite(view.projection_scale) || view.projection_scale <= 0.0f) {
		return;
	}
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	storage->cluster_streaming_begin_frame();
	RenderList &list = render_list[RENDER_LIST_SECONDARY];
	for (uint32_t i = p_from; i < p_from + p_count; i++) {
		GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		RenderElementInfo &element = list.element_info[i];
		// _fill_instance_data() kept eligible surfaces apart.
		if (element.repeat != 1) {
			i += element.repeat - 1;
			continue;
		}
		if (element.lod_index != 0 || !_mesh_clod_surface_eligible(surface)) {
			continue;
		}
		// The shadow material draws: it must keep the source positions too.
		const SceneShaderForwardClustered::ShaderData *shader = surface->shader_shadow;
		if (!shader || shader->uses_vertex || shader->uses_position || shader->uses_world_coordinates || shader->writes_modelview_or_projection || shader->uses_custom_vertex_transform || shader->uses_alpha || shader->uses_depth_prepass_alpha) {
			continue;
		}
		const auto &resident = storage->mesh_surface_get_cluster_data(surface->surface);
		if (shadow.total_groups + resident.group_count > (1u << 20) || shadow.inputs.size() >= 16384) {
			continue;
		}
		MeshCLODInput input;
		if (!_mesh_clod_make_input(surface->owner->transform, view, resident.group_count, shadow.total_groups * 5, input)) {
			continue;
		}
		element.mesh_clod_offset = input.params[1];
		element.mesh_clod_draw = shadow.inputs.size();
		shadow.total_groups += resident.group_count;
		shadow.inputs.push_back(input);
		shadow.work.push_back({ resident.groups, resident.group_count, resident.streaming });
	}
}

void RenderForwardClustered::_mesh_clod_shadow_dispatch() {
	MeshCLOD::Shadow &shadow = mesh_clod.shadow;
	shadow.active = false;
	if (shadow.work.is_empty()) {
		return;
	}
	RD *rd = RD::get_singleton();
	bool valid = _mesh_clod_pipelines();
	if (valid && shadow.group_capacity < shadow.total_groups) {
		for (RID *buffer : { &shadow.commands, &shadow.selection }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
		}
		shadow.group_capacity = Math::next_power_of_2(shadow.total_groups);
		shadow.commands = rd->storage_buffer_create(shadow.group_capacity * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		shadow.selection = rd->storage_buffer_create(shadow.group_capacity * sizeof(uint32_t));
		valid = shadow.commands.is_valid() && shadow.selection.is_valid();
		if (!valid) {
			shadow.group_capacity = 0;
		}
	}
	if (valid && shadow.draw_capacity < shadow.inputs.size()) {
		for (RID *buffer : { &shadow.inputs_buffer, &shadow.counts }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
		}
		shadow.draw_capacity = Math::next_power_of_2(shadow.inputs.size());
		shadow.inputs_buffer = rd->storage_buffer_create(shadow.draw_capacity * sizeof(MeshCLODInput));
		shadow.counts = rd->storage_buffer_create(shadow.draw_capacity * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		valid = shadow.inputs_buffer.is_valid() && shadow.counts.is_valid();
		if (!valid) {
			shadow.draw_capacity = 0;
		}
	}
	if (valid && shadow.statistics.is_null()) {
		// Words 2-5 of the viewport layout; nothing reads them for shadows.
		shadow.statistics = rd->storage_buffer_create(8 * sizeof(uint32_t));
		valid = shadow.statistics.is_valid();
	}
	if (!valid) {
		// Source draws: every element forgets its selection.
		RenderList &list = render_list[RENDER_LIST_SECONDARY];
		for (uint32_t i = 0; i < list.element_info.size(); i++) {
			list.element_info[i].mesh_clod_offset = UINT32_MAX;
		}
		shadow.inputs.clear();
		shadow.work.clear();
		shadow.total_groups = 0;
		return;
	}
	shadow.compact = mesh_clod.multi_draw && mesh_clod.indirect_count && GLOBAL_GET_CACHED(bool, "rendering/geometry/cluster_lod/compact_commands");
	if (shadow.compact) {
		rd->buffer_clear(shadow.counts, 0, shadow.inputs.size() * sizeof(uint32_t));
	}
	rd->buffer_update(shadow.inputs_buffer, 0, shadow.inputs.size() * sizeof(MeshCLODInput), shadow.inputs.ptr());
	RENDER_TIMESTAMP("Shadow CLOD Selection");
	_mesh_clod_dispatch(shadow.work, shadow.inputs_buffer, shadow.commands, shadow.selection, shadow.counts, shadow.statistics, shadow.compact);
	shadow.inputs.clear();
	shadow.work.clear();
	shadow.total_groups = 0;
	shadow.active = true;
}
