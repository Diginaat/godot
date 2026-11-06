/**************************************************************************/
/*  multimesh_culling.cpp                                                  */
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

bool RenderForwardClustered::_multimesh_culling_available(const RenderBufferDataForwardClustered *p_buffers) const {
	// Preserve conventional precision until double builds have culling tests.
#ifdef REAL_T_IS_DOUBLE
	return false;
#else
	// The kernels always bind the statistics buffer; without it, keep direct draws.
	if (!p_buffers || p_buffers->geometry_statistics.buffer.is_null() || !bool(GLOBAL_GET("rendering/geometry/multimesh_gpu_culling/enabled"))) {
		return false;
	}
	RD *rd = RD::get_singleton();
	return rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS) >= 256 && rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X) >= 256;
#endif
}

void RenderForwardClustered::_prepare_multimesh_culling(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	if (!p_buffers) {
		return;
	}
	p_buffers->multimesh_cull.active = false;
	// Preserve conventional reflection and multiview semantics until those
	// configurations have dedicated culling tests.
	if (!_multimesh_culling_available(p_buffers) || p_render_data->reflection_probe.is_valid() || p_render_data->scene_data->view_count != 1 ||
			p_render_data->scene_data->mirror_pass) {
		return;
	}
	MultiMeshCullView view;
	view.planes = _jitter_safe_projection(p_render_data->scene_data->cam_projection, p_render_data->scene_data->taa_jitter).get_projection_planes(p_render_data->scene_data->cam_transform);
	view.lod_threshold = p_render_data->scene_data->screen_mesh_lod_threshold;
	view.lod_distance_multiplier = p_render_data->scene_data->lod_distance_multiplier;
	view.lod_allowed = view.lod_threshold > 0.0f && !p_render_data->scene_data->cam_orthogonal;
	view.lod_camera = p_render_data->scene_data->main_cam_transform.origin;
	view.statistics_offset = 0;
	MultiMeshCullBatch &batch = multimesh_cull_batch;
	batch.clear();
	for (RenderListType list_type : { RENDER_LIST_OPAQUE, RENDER_LIST_MOTION }) {
		_collect_multimesh_culling(list_type, 0, render_list[list_type].elements.size(), view, false, batch);
	}
	p_buffers->geometry_statistics.skipped_surfaces += batch.skipped_surfaces;
	p_buffers->geometry_statistics.skipped_instances += batch.skipped_instances;
	if (_dispatch_multimesh_culling(batch, p_buffers->multimesh_cull, p_buffers->geometry_statistics.buffer, "MultiMesh Frustum Culling")) {
		p_buffers->geometry_statistics.culled_surfaces = batch.elements.size();
	}
}

void RenderForwardClustered::_prepare_multimesh_shadow_culling(RenderBufferDataForwardClustered *p_buffers) {
	scene_state.shadow_multimesh_cull_commands = RID();
	if (!p_buffers) {
		return;
	}
	p_buffers->multimesh_shadow_cull.active = false;
	// Separate opt-in: in the measured valley the cascades see most casters, so
	// the extra dispatches cost render-thread time without a GPU gain.
	if (!_multimesh_culling_available(p_buffers) || !bool(GLOBAL_GET("rendering/geometry/multimesh_gpu_culling/shadows"))) {
		return;
	}
	MultiMeshCullBatch &batch = multimesh_cull_batch;
	batch.clear();
	for (const SceneState::ShadowPass &shadow_pass : scene_state.shadow_passes) {
		// A dual-paraboloid projection is not a frustum; keep its direct draws.
		if (shadow_pass.pass_mode != PASS_MODE_SHADOW || shadow_pass.element_count == 0) {
			continue;
		}
		MultiMeshCullView view;
		view.planes = shadow_pass.cam_projection.get_projection_planes(shadow_pass.cam_transform);
		if (view.planes.size() != 6) {
			continue;
		}
		// Shadow pancaking clamps casters in front of the near plane onto it, so
		// the near plane must never cull; repeat the far plane in its slot.
		view.planes.write[Projection::PLANE_NEAR] = view.planes[Projection::PLANE_FAR];
		// Shadow LOD follows the main camera, as in _fill_render_list().
		view.lod_threshold = shadow_pass.screen_mesh_lod_threshold;
		view.lod_distance_multiplier = shadow_pass.lod_distance_multiplier;
		view.lod_allowed = view.lod_threshold > 0.0f;
		view.lod_camera = shadow_pass.main_cam_transform.origin;
		view.statistics_offset = GEOMETRY_STATISTICS_SHADOW_OFFSET;
		_collect_multimesh_culling(RENDER_LIST_SECONDARY, shadow_pass.element_from, shadow_pass.element_count, view, true, batch);
	}
	if (_dispatch_multimesh_culling(batch, p_buffers->multimesh_shadow_cull, p_buffers->geometry_statistics.buffer, "MultiMesh Shadow Culling")) {
		scene_state.shadow_multimesh_cull_commands = p_buffers->multimesh_shadow_cull.commands;
		p_buffers->geometry_statistics.shadow_culled_surfaces += batch.elements.size();
	}
}

void RenderForwardClustered::_collect_multimesh_culling(RenderListType p_list, uint32_t p_from, uint32_t p_count, const MultiMeshCullView &p_view, bool p_shadow, MultiMeshCullBatch &r_batch) {
	static_assert(sizeof(MultiMeshCullInput) == 192);
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	RD *rd = RD::get_singleton();
	const uint64_t max_instances = uint64_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)) * 64;
	const uint32_t pass_flag = p_shadow ? GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW : GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE;
	RenderList &list = render_list[p_list];
	for (uint32_t i = p_from; i < p_from + p_count; i++) {
		GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		GeometryInstanceForwardClustered *instance = surface->owner;
		const uint32_t excluded = INSTANCE_DATA_FLAG_PARTICLES | INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D | INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT;
		if (!(instance->base_flags & INSTANCE_DATA_FLAG_MULTIMESH) || (instance->base_flags & excluded) ||
				instance->instance_count == 0 || instance->instance_count > max_instances ||
				surface->surface_shadow != surface->surface || !(surface->flags & pass_flag) ||
				!instance->transform.is_finite()) {
			continue;
		}
		// Shadow passes draw with the shadow shader; both must keep the ordinary transform.
		bool supported = true;
		for (const SceneShaderForwardClustered::ShaderData *shader : { surface->shader, p_shadow ? surface->shader_shadow : surface->shader }) {
			if (!shader || shader->uses_vertex || shader->uses_position || shader->uses_world_coordinates || shader->writes_modelview_or_projection || shader->uses_custom_vertex_transform ||
					shader->uses_z_clip_scale || shader->uses_point_size || shader->uses_alpha || shader->uses_blend_alpha ||
					shader->uses_depth_prepass_alpha || shader->stencil_enabled) {
				supported = false;
				break;
			}
		}
		if (!supported) {
			continue;
		}
		// Surfaces with index LODs choose a level per visible instance on the
		// GPU, using Godot's CPU LOD rule per instance instead of once for the
		// whole MultiMesh. Level 0 is the CPU LOD of the nearest point of the
		// bounds; the farthest point bounds the coarsest LOD any instance needs.
		// Up to four levels are bucketed over that range.
		// Distance thinning (MultiMesh.distance_thinning_*), in local units: local
		// distance times the smallest node scale never exceeds the world distance,
		// so dividing by that scale keeps at least the requested fraction.
		const Vector3 node_scale_abs = instance->transform.basis.get_scale_abs();
		const float smallest_scale = MIN(node_scale_abs.x, MIN(node_scale_abs.y, node_scale_abs.z));
		float thinning_begin = 0.0f;
		float thinning_end = 0.0f;
		storage->multimesh_get_distance_thinning(instance->data->base, thinning_begin, thinning_end);
		const bool thinning = thinning_end > 0.0f && smallest_scale > 0.0f && Math::is_finite(thinning_end / smallest_scale);
		const uint32_t first_lod = list.element_info[i].lod_index;
		uint32_t last_lod = first_lod;
		uint32_t levels = 1;
		float lod_edges[4] = {};
		if (p_view.lod_allowed && storage->mesh_surface_has_lod(surface->surface)) {
			// Local distance times the smallest node scale never exceeds the
			// world distance, so non-uniform nodes err towards finer levels.
			const Vector3 node_scale = instance->transform.basis.get_scale_abs();
			const float min_node_scale = MIN(node_scale.x, MIN(node_scale.y, node_scale.z));
			const float edge_scale = instance->lod_model_scale * instance->lod_bias / (p_view.lod_threshold * p_view.lod_distance_multiplier * min_node_scale);
			const AABB &aabb = instance->transformed_aabb;
			const Vector3 farthest = (p_view.lod_camera - aabb.position).abs().max((p_view.lod_camera - aabb.position - aabb.size).abs());
			uint32_t index_count = 0;
			last_lod = MAX(first_lod, storage->mesh_surface_get_lod(surface->surface, instance->lod_model_scale * instance->lod_bias, farthest.length() * p_view.lod_distance_multiplier, p_view.lod_threshold, index_count));
			if (Math::is_finite(edge_scale) && edge_scale > 0.0f && last_lod > first_lod) {
				levels = MIN(last_lod - first_lod + 1, 4u);
				for (uint32_t level = 0; level + 1 < levels; level++) {
					lod_edges[level] = storage->mesh_surface_get_lod_edge_length(surface->surface, _multimesh_lod_for_bucket(level + 1, first_lod, last_lod) - 1) * edge_scale;
				}
			} else {
				last_lod = first_lod;
			}
		}
		// When the whole MultiMesh lies inside every plane, culling cannot reject
		// an instance: keep the direct draw and skip three dispatches. A wrong or
		// too small custom AABB only draws everything (source behavior), never
		// removes geometry; a non-finite bound keeps the GPU culling path.
		// Surfaces spanning several LODs still need the dispatch to choose levels.
		bool fully_inside = levels == 1 && !thinning;
		for (const Plane &plane : p_view.planes) {
			if (!fully_inside) {
				break;
			}
			fully_inside = plane.distance_to(instance->transformed_aabb.get_support(plane.normal)) <= 0.0f;
		}
		if (fully_inside) {
			r_batch.skipped_surfaces++;
			r_batch.skipped_instances += instance->instance_count;
			continue;
		}
		// Excess work uses the original direct draw, never overflow-induced holes.
		const uint32_t reserved_ids = (instance->instance_count + 63u) & ~63u;
		if (uint64_t(r_batch.total_ids) + uint64_t(reserved_ids) * levels > (1u << 23) || r_batch.inputs.size() >= 65536) {
			continue;
		}
		RID source = storage->_multimesh_get_buffer_rd_rid(instance->data->base);
		if (source.is_null()) {
			continue;
		}
		MultiMeshCullInput input = {};
		const Basis transposed = instance->transform.basis.transposed();
		for (uint32_t plane_index = 0; plane_index < 6; plane_index++) {
			const Plane &plane = p_view.planes[plane_index];
			// Pull back the plane covector, without inverse or normalization.
			const Vector3 normal = transposed.xform(plane.normal);
			input.planes[plane_index][0] = normal.x;
			input.planes[plane_index][1] = normal.y;
			input.planes[plane_index][2] = normal.z;
			input.planes[plane_index][3] = plane.d - plane.normal.dot(instance->transform.origin);
		}
		const AABB bounds = storage->mesh_surface_get_aabb(surface->surface);
		const Vector3 center = bounds.get_center();
		for (uint32_t axis = 0; axis < 3; axis++) {
			input.center[axis] = center[axis];
			input.half_extents[axis] = bounds.size[axis] * 0.5f;
		}
		if (thinning) {
			input.center[3] = thinning_begin / smallest_scale;
			input.half_extents[3] = thinning_end / smallest_scale;
		}
		input.count = instance->instance_count;
		input.stride = 3 + uint32_t(storage->multimesh_uses_colors(instance->data->base)) + uint32_t(storage->multimesh_uses_custom_data(instance->data->base));
		uint32_t previous_offset;
		storage->_multimesh_get_motion_vectors_offsets(instance->data->base, input.current_offset, previous_offset);
		input.ids_offset = r_batch.total_ids;
		r_batch.total_ids += reserved_ids * levels;
		const Vector3 local_camera = instance->transform.affine_inverse().xform(p_view.lod_camera);
		for (uint32_t axis = 0; axis < 3; axis++) {
			input.lod_camera[axis] = local_camera[axis];
		}
		memcpy(input.lod_edges, lod_edges, sizeof(lod_edges));
		if (!local_camera.is_finite()) {
			levels = 1; // Keeps the reserved range; level 0 is always conservative.
		}
		if (levels == 1) {
			last_lod = first_lod;
		}
		input.lod_levels = levels;
		input.lod_stride = reserved_ids;
		input.first_command = r_batch.commands.size() / 5;
		input.statistics_offset = p_view.statistics_offset;
		r_batch.inputs.push_back(input);
		r_batch.transforms.push_back(source);
		r_batch.elements.push_back(&list.element_info[i]);
		r_batch.last_lods.push_back(last_lod);
		// One five-word indexed command per level; the kernel writes instance counts.
		for (uint32_t level = 0; level < levels; level++) {
			const uint32_t lod = _multimesh_lod_for_bucket(level, first_lod, last_lod);
			r_batch.commands.push_back(lod == 0 ? storage->mesh_surface_get_vertices_drawn_count(surface->surface) : storage->mesh_surface_get_index_count(surface->surface, lod));
			for (uint32_t word = 1; word < 5; word++) {
				r_batch.commands.push_back(0);
			}
		}
	}
}

bool RenderForwardClustered::_dispatch_multimesh_culling(MultiMeshCullBatch &p_batch, RenderBufferDataForwardClustered::MultiMeshCullBuffers &r_buffers, RID p_statistics, const String &p_label) {
	if (p_batch.inputs.is_empty()) {
		return false;
	}
	RD *rd = RD::get_singleton();
	// CPU uploads only per-surface metadata, not per-instance transforms/visibility.
	if (r_buffers.id_capacity < p_batch.total_ids) {
		for (RID resource : { r_buffers.visible_ids, r_buffers.ranks, r_buffers.blocks }) {
			if (resource.is_valid()) {
				rd->free_rid(resource);
			}
		}
		r_buffers.id_capacity = Math::next_power_of_2(p_batch.total_ids);
		r_buffers.visible_ids = rd->storage_buffer_create(r_buffers.id_capacity * sizeof(uint32_t));
		r_buffers.ranks = rd->storage_buffer_create(r_buffers.id_capacity * sizeof(uint32_t));
		r_buffers.blocks = rd->storage_buffer_create((r_buffers.id_capacity / 64) * sizeof(uint32_t));
		if (r_buffers.visible_ids.is_null() || r_buffers.ranks.is_null() || r_buffers.blocks.is_null()) {
			r_buffers.id_capacity = 0;
			return false;
		}
	}
	const uint32_t draw_count = p_batch.inputs.size();
	if (r_buffers.draw_capacity < draw_count) {
		for (RID resource : { r_buffers.commands, r_buffers.inputs }) {
			if (resource.is_valid()) {
				rd->free_rid(resource);
			}
		}
		r_buffers.draw_capacity = Math::next_power_of_2(draw_count);
		// Up to four LOD command records per draw.
		r_buffers.commands = rd->storage_buffer_create(r_buffers.draw_capacity * 4 * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		r_buffers.inputs = rd->storage_buffer_create(r_buffers.draw_capacity * sizeof(MultiMeshCullInput));
		if (r_buffers.commands.is_null() || r_buffers.inputs.is_null()) {
			r_buffers.draw_capacity = 0;
			return false;
		}
	}
	if (multimesh_cull.version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		modes.push_back("\n#define MODE_SCAN\n");
		modes.push_back("\n#define MODE_SCATTER\n");
		multimesh_cull.shader.initialize(modes, "\n#define GEOMETRY_STATISTICS\n");
		multimesh_cull.version = multimesh_cull.shader.version_create();
		for (uint32_t mode = 0; mode < 3; mode++) {
			multimesh_cull.pipelines[mode] = rd->compute_pipeline_create(multimesh_cull.shader.version_get_shader(multimesh_cull.version, mode));
		}
	}
	for (RID pipeline : multimesh_cull.pipelines) {
		if (pipeline.is_null()) {
			return false;
		}
	}
	rd->buffer_update(r_buffers.commands, 0, p_batch.commands.size() * sizeof(uint32_t), p_batch.commands.ptr());
	rd->buffer_update(r_buffers.inputs, 0, draw_count * sizeof(MultiMeshCullInput), p_batch.inputs.ptr());
	rd->capture_timestamp(p_label);
	// Set 0 (shared buffers) is bound once per mode; only the cull mode binds
	// each MultiMesh's transform buffer (set 1) per draw.
	LocalVector<RD::Uniform> shared;
	const RID resources[6] = { r_buffers.inputs, r_buffers.visible_ids, r_buffers.commands, r_buffers.ranks, r_buffers.blocks, p_statistics };
	for (uint32_t binding = 0; binding < 6; binding++) {
		RD::Uniform uniform;
		uniform.binding = binding;
		uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		uniform.append_id(resources[binding]);
		shared.push_back(uniform);
	}
	RD::ComputeListID compute_list = rd->compute_list_begin();
	for (uint32_t mode = 0; mode < 3; mode++) {
		const RID shader = multimesh_cull.shader.version_get_shader(multimesh_cull.version, mode);
		rd->compute_list_bind_compute_pipeline(compute_list, multimesh_cull.pipelines[mode]);
		rd->compute_list_bind_uniform_set(compute_list, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, shared), 0);
		RID previous_transforms;
		for (uint32_t draw = 0; draw < draw_count; draw++) {
			if (mode == 0 && p_batch.transforms[draw] != previous_transforms) {
				// Consecutive surfaces of one MultiMesh share the transform set.
				RD::Uniform transforms(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_batch.transforms[draw]);
				rd->compute_list_bind_uniform_set(compute_list, UniformSetCacheRD::get_singleton()->get_cache(shader, 1, transforms), 1);
				previous_transforms = p_batch.transforms[draw];
			}
			rd->compute_list_set_push_constant(compute_list, &draw, sizeof(draw));
			rd->compute_list_dispatch(compute_list, mode == 1 ? 1 : (p_batch.inputs[draw].count + 63) / 64, 1, 1);
		}
		if (mode != 2) {
			rd->compute_list_add_barrier(compute_list);
		}
	}
	rd->compute_list_end();
	rd->capture_timestamp(p_label + " End");
	for (uint32_t draw = 0; draw < draw_count; draw++) {
		RenderElementInfo *element = p_batch.elements[draw];
		element->multimesh_cull_draw = p_batch.inputs[draw].first_command;
		element->multimesh_visible_ids_offset = p_batch.inputs[draw].ids_offset;
		element->multimesh_cull_lod = (p_batch.inputs[draw].lod_levels << 24) | p_batch.inputs[draw].lod_stride;
		element->multimesh_cull_last_lod = p_batch.last_lods[draw];
	}
	r_buffers.active = true;
	return true;
}
