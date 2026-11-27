/**************************************************************************/
/*  mesh_hiz.cpp                                                          */
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

#include <limits>

using namespace RendererSceneRenderImplementation;

namespace {
struct OcclusionInput {
	float local_to_clip[16];
	float clip_error[4];
	uint32_t params[4];
	uint32_t history[4];
};
static_assert(sizeof(OcclusionInput) == 112);

const StringName &hzb_scope() {
	static const StringName scope("mesh_clod_hzb");
	return scope;
}

const StringName &hzb_name() {
	static const StringName name("depth");
	return name;
}

// Frees and recreates a storage buffer when it is too small. Returns false if the allocation fails.
bool ensure_buffer(RID &r_buffer, uint32_t &r_capacity, uint32_t p_needed, uint32_t p_element_size, BitField<RD::StorageBufferUsage> p_usage = 0) {
	if (r_buffer.is_valid() && r_capacity >= p_needed) {
		return true;
	}
	RD *rd = RD::get_singleton();
	if (r_buffer.is_valid()) {
		rd->free_rid(r_buffer);
	}
	r_capacity = Math::next_power_of_2(MAX(p_needed, 1u));
	r_buffer = rd->storage_buffer_create(r_capacity * p_element_size, {}, p_usage);
	if (r_buffer.is_null()) {
		r_capacity = 0;
		return false;
	}
	return true;
}
} // namespace

bool RenderForwardClustered::_hzb_possible(const RenderDataRD *p_render_data, const RenderBufferDataForwardClustered *p_buffers) const {
	// TAA and upscaler jitter is allowed: get_cam_projection() includes this
	// frame's jitter, so test rectangles match the jittered depth they are tested
	// against, and the colour pass rasterizes with the same projection.
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	return p_buffers && p_buffers->geometry_statistics.buffer.is_valid() && rb.is_valid() && rb->get_msaa_3d() == RSE::VIEWPORT_MSAA_DISABLED &&
			p_render_data->compositor.is_null() && !p_render_data->render_region.has_area();
}

bool RenderForwardClustered::_setup_hzb(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	p_buffers->hzb.ready = false;
	if (!_hzb_possible(p_render_data, p_buffers)) {
		return false;
	}
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		// Custom fragment depth can invalidate geometric nearest-depth bounds,
		// including when the material is used only as an occluder.
		if (list.elements[i]->shader->writes_fragment_depth || list.elements[i]->shader->stencil_enabled) {
			return false;
		}
	}
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	const Size2i size = rb->get_internal_size();
	const Size2i padded(Math::next_power_of_2(uint32_t(size.width)), Math::next_power_of_2(uint32_t(size.height)));
	// Twice the base image bounds the full mip chain even for 1-pixel strips.
	if (uint64_t(padded.width) * padded.height * 8 > 64 * 1024 * 1024) {
		return false;
	}
	RD *rd = RD::get_singleton();
	const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	if (!rd->texture_is_format_supported_for_usage(RD::DATA_FORMAT_R32_SFLOAT, usage)) {
		return false;
	}
	uint32_t mipmaps = 1;
	for (uint32_t extent = MAX(padded.width, padded.height); extent > 1; extent >>= 1) {
		mipmaps++;
	}
	if (!rb->has_texture(hzb_scope(), hzb_name())) {
		if (rb->create_texture(hzb_scope(), hzb_name(), RD::DATA_FORMAT_R32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, padded, 1, mipmaps).is_null()) {
			return false;
		}
	}
	if (mesh_hiz.reduce_version.is_null()) {
		Vector<String> modes;
		modes.push_back("\n#define MODE_COPY\n");
		modes.push_back("");
		mesh_hiz.reduce_shader.initialize(modes);
		mesh_hiz.reduce_version = mesh_hiz.reduce_shader.version_create();
		for (uint32_t mode = 0; mode < 2; mode++) {
			mesh_hiz.reduce_pipelines[mode] = rd->compute_pipeline_create(mesh_hiz.reduce_shader.version_get_shader(mesh_hiz.reduce_version, mode));
		}
		Vector<String> occlude_modes;
		occlude_modes.push_back("");
		occlude_modes.push_back("\n#define MODE_SPLIT\n");
		mesh_hiz.occlude_shader.initialize(occlude_modes, "\n#define GEOMETRY_STATISTICS\n");
		mesh_hiz.occlude_version = mesh_hiz.occlude_shader.version_create();
		mesh_hiz.occlude_pipeline = rd->compute_pipeline_create(mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 0));
		mesh_hiz.split_pipeline = rd->compute_pipeline_create(mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 1));
	}
	if (mesh_hiz.reduce_pipelines[0].is_null() || mesh_hiz.reduce_pipelines[1].is_null() || mesh_hiz.occlude_pipeline.is_null() || mesh_hiz.split_pipeline.is_null()) {
		return false;
	}
	p_buffers->hzb.mipmaps = mipmaps;
	p_buffers->hzb.ready = true;
	return true;
}

void RenderForwardClustered::_build_hzb(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	if (!p_buffers->hzb.ready) {
		return;
	}
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	RD *rd = RD::get_singleton();
	const Size2i size = rb->get_internal_size();
	const Size2i padded(Math::next_power_of_2(uint32_t(size.width)), Math::next_power_of_2(uint32_t(size.height)));
	const uint32_t mipmaps = p_buffers->hzb.mipmaps;
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	rd->capture_timestamp("Mesh CLOD HZB");
	RD::ComputeListID compute = rd->compute_list_begin();
	for (uint32_t mip = 0; mip < mipmaps; mip++) {
		const uint32_t mode = mip == 0 ? 0 : 1;
		const RID shader = mesh_hiz.reduce_shader.version_get_shader(mesh_hiz.reduce_version, mode);
		const RID source = mip == 0 ? rb->get_depth_texture(0) : rb->get_texture_slice(hzb_scope(), hzb_name(), 0, mip - 1);
		const RID target = rb->get_texture_slice(hzb_scope(), hzb_name(), 0, mip);
		const Size2i source_size = mip == 0 ? size : Size2i(MAX(1, padded.width >> (mip - 1)), MAX(1, padded.height >> (mip - 1)));
		const Size2i target_size(MAX(1, padded.width >> mip), MAX(1, padded.height >> mip));
		const int32_t push[4] = { source_size.width, source_size.height, target_size.width, target_size.height };
		LocalVector<RD::Uniform> uniforms;
		RD::Uniform source_uniform;
		source_uniform.binding = 0;
		source_uniform.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		source_uniform.append_id(sampler);
		source_uniform.append_id(source);
		uniforms.push_back(source_uniform);
		RD::Uniform target_uniform;
		target_uniform.binding = 1;
		target_uniform.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		target_uniform.append_id(target);
		uniforms.push_back(target_uniform);
		rd->compute_list_bind_compute_pipeline(compute, mesh_hiz.reduce_pipelines[mode]);
		rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		rd->compute_list_set_push_constant(compute, push, sizeof(push));
		rd->compute_list_dispatch(compute, (target_size.width + 7) / 8, (target_size.height + 7) / 8, 1);
		if (mip + 1 < mipmaps) {
			rd->compute_list_add_barrier(compute);
		}
	}
	rd->compute_list_end();

	// The directional shadow passes run after the depth pre-pass (phase 2
	// included): their receiver masks read its final depth. The pyramid's
	// conditions (no MSAA, no custom depth) hold for it too.
	shadow_receivers.view_valid = p_render_data->scene_data->view_count == 1;
	shadow_receivers.depth_texture = rb->get_depth_texture(0);
	shadow_receivers.size = size;
	shadow_receivers.volumetric_fog = p_render_data->environment.is_valid() && environment_get_volumetric_fog_enabled(p_render_data->environment);
	shadow_receivers.cam_projection = p_render_data->scene_data->get_cam_projection();
	shadow_receivers.cam_transform = p_render_data->scene_data->cam_transform;
}

bool RenderForwardClustered::_setup_mesh_clod_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &buffers = p_buffers->mesh_clod;
	buffers.occlusion_ready = false;
	buffers.two_pass = false;
	if (!p_buffers->hzb.ready || !buffers.active || !bool(GLOBAL_GET("rendering/geometry/cluster_lod/occlusion_enabled"))) {
		return false;
	}
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	const Size2i size = p_render_data->render_buffers->get_internal_size();
	RD *rd = RD::get_singleton();
	Vector<OcclusionInput> inputs;
	buffers.occlusion_work.clear();
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	const Projection projection = p_render_data->scene_data->get_cam_projection();
	const Projection view(p_render_data->scene_data->cam_transform.affine_inverse());
	const Projection view_projection = projection * view;
	// History words needed by surfaces without a slot. The arena only appends;
	// when it is full every slot is forgotten and the buffer starts cleared
	// (all visible), before any surface of this frame takes an offset.
	uint32_t all_groups = 0;
	uint32_t new_groups = 0;
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		if (list.element_info[i].mesh_clod_offset != UINT32_MAX) {
			const uint32_t count = storage->mesh_surface_get_cluster_data(list.elements[i]->surface).group_count;
			const auto *slot = buffers.history_slots.getptr(uint64_t(uintptr_t(list.elements[i])));
			all_groups += count;
			new_groups += (!slot || slot->count != count) ? count : 0;
		}
	}
	if (buffers.history.is_null() || buffers.history_used + new_groups > buffers.history_capacity) {
		if (buffers.history.is_valid()) {
			rd->free_rid(buffers.history);
		}
		const uint32_t capacity = Math::next_power_of_2(MAX(all_groups * 2, 1024u));
		buffers.history = rd->storage_buffer_create(capacity * sizeof(uint32_t));
		buffers.history_capacity = buffers.history.is_valid() ? capacity : 0;
		buffers.history_used = 0;
		buffers.history_slots.clear();
		if (buffers.history.is_null()) {
			return false;
		}
		rd->buffer_clear(buffers.history, 0, capacity * sizeof(uint32_t));
	}
	uint32_t groups_total = 0;
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		if (list.element_info[i].mesh_clod_offset == UINT32_MAX) {
			continue;
		}
		const GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		const auto &resident = storage->mesh_surface_get_cluster_data(surface->surface);
		OcclusionInput input = {};
		RendererRD::MaterialStorage::store_camera(view_projection * Projection(surface->owner->transform), input.local_to_clip);
		// Bound accumulated float rounding by abs(P)*abs(V)*abs(M)*max(abs(local)).
		// This grows with large cancelling world translations: uncertain groups
		// retain geometry instead of trusting a fragile single-precision rectangle.
		const AABB bounds = storage->mesh_surface_get_aabb(surface->surface);
		const Vector3 maximum = bounds.position.abs().max((bounds.position + bounds.size).abs()) + Vector3(1, 1, 1);
		double magnitudes[4] = { maximum.x, maximum.y, maximum.z, 1.0 };
		for (const Projection &matrix : { Projection(surface->owner->transform), view, projection }) {
			double next[4] = {};
			for (uint32_t row = 0; row < 4; row++) {
				for (uint32_t column = 0; column < 4; column++) {
					next[row] += Math::abs(double(matrix[column][row])) * magnitudes[column];
				}
			}
			memcpy(magnitudes, next, sizeof(magnitudes));
		}
		for (uint32_t axis = 0; axis < 4; axis++) {
			input.clip_error[axis] = float(magnitudes[axis] * (64.0 * std::numeric_limits<float>::epsilon()));
		}
		input.params[0] = resident.group_count;
		input.params[1] = list.element_info[i].mesh_clod_offset;
		input.params[2] = size.width;
		input.params[3] = size.height;
		// History slot of this surface (allocated above when new or resized).
		const uint64_t key = uint64_t(uintptr_t(surface));
		auto slot = buffers.history_slots.find(key);
		if (!slot || slot->value.count != resident.group_count) {
			slot = buffers.history_slots.insert(key, { buffers.history_used, resident.group_count });
			buffers.history_used += resident.group_count;
		}
		input.history[0] = slot->value.offset;
		input.history[1] = list.element_info[i].mesh_clod_draw;
		groups_total = MAX(groups_total, input.params[1] / 5 + resident.group_count);
		inputs.push_back(input);
		buffers.occlusion_work.push_back({ resident.groups, resident.group_count });
	}
	if (buffers.occlusion_work.is_empty()) {
		return false;
	}
	if (!ensure_buffer(buffers.occlusion_inputs, buffers.occlusion_capacity, inputs.size(), sizeof(OcclusionInput))) {
		return false;
	}
	// Pass streams have the selection command buffer's layout. The colour
	// stream is always needed; the depth streams only with two passes.
	const uint32_t command_words = buffers.group_capacity * 5;
	if (buffers.pass_command_capacity != command_words) {
		for (RID *buffer : { &buffers.color_commands, &buffers.depth_commands, &buffers.phase2_commands }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
				*buffer = RID();
			}
		}
		buffers.pass_command_capacity = command_words;
	}
	const bool two_pass = GLOBAL_GET("rendering/geometry/cluster_lod/occlusion_two_pass");
	RID *const streams[3] = { &buffers.color_commands, &buffers.depth_commands, &buffers.phase2_commands };
	for (uint32_t stream = 0; stream < (two_pass ? 3u : 1u); stream++) {
		if (streams[stream]->is_null()) {
			*streams[stream] = rd->storage_buffer_create(command_words * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		}
		if (streams[stream]->is_null()) {
			buffers.pass_command_capacity = 0;
			return false;
		}
	}
	if (groups_total > buffers.group_capacity) {
		return false;
	}
	rd->buffer_update(buffers.occlusion_inputs, 0, inputs.size() * sizeof(OcclusionInput), inputs.ptr());
	buffers.occlusion_ready = true;
	buffers.two_pass = two_pass;
	return true;
}

void RenderForwardClustered::_split_mesh_clod_occlusion(RenderBufferDataForwardClustered *p_buffers) {
	auto &buffers = p_buffers->mesh_clod;
	if (!buffers.occlusion_ready || !buffers.two_pass) {
		return;
	}
	RD *rd = RD::get_singleton();
	const RID shader = mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 1);
	rd->capture_timestamp("Mesh CLOD HZB split");
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, mesh_hiz.split_pipeline);
	for (uint32_t draw = 0; draw < buffers.occlusion_work.size(); draw++) {
		LocalVector<RD::Uniform> uniforms;
		const RID resources[6] = { buffers.occlusion_work[draw].groups, buffers.occlusion_inputs, buffers.selection, buffers.history, buffers.depth_commands, buffers.counts };
		const uint32_t bindings[6] = { 0, 1, 2, 5, 6, 8 };
		for (uint32_t i = 0; i < 6; i++) {
			RD::Uniform uniform;
			uniform.binding = bindings[i];
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.append_id(resources[i]);
			uniforms.push_back(uniform);
		}
		RD::Uniform statistics;
		statistics.binding = 4;
		statistics.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		statistics.append_id(p_buffers->geometry_statistics.buffer);
		uniforms.push_back(statistics);
		rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		const uint32_t push[4] = { draw, 1, buffers.compact ? 1u : 0u, buffers.draw_capacity };
		rd->compute_list_set_push_constant(compute, push, sizeof(push));
		rd->compute_list_dispatch(compute, (buffers.occlusion_work[draw].count + 63) / 64, 1, 1);
	}
	rd->compute_list_end();
}


namespace {
// Matches OcclusionData in multimesh_cull.glsl.
struct MultiMeshOcclusionInput {
	float local_to_clip[16];
	float abs_to_clip[16];
	uint32_t params[4];
};
static_assert(sizeof(MultiMeshOcclusionInput) == 144);
} // namespace

bool RenderForwardClustered::_setup_multimesh_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &cull = p_buffers->multimesh_cull;
	cull.occlusion_ready = false;
	if (!p_buffers->hzb.ready || !cull.active || cull.draws.is_empty() || !bool(GLOBAL_GET("rendering/geometry/multimesh_gpu_culling/occlusion"))) {
		return false;
	}
	RD *rd = RD::get_singleton();
	const Size2i size = p_render_data->render_buffers->get_internal_size();
	const Projection projection = p_render_data->scene_data->get_cam_projection();
	const Projection view(p_render_data->scene_data->cam_transform.affine_inverse());
	const Projection view_projection = projection * view;
	LocalVector<MultiMeshOcclusionInput> inputs;
	inputs.resize(cull.draws.size());
	for (uint32_t draw = 0; draw < cull.draws.size(); draw++) {
		const Projection model(cull.draws[draw].transform);
		MultiMeshOcclusionInput &input = inputs[draw];
		RendererRD::MaterialStorage::store_camera(view_projection * model, input.local_to_clip);
		// abs(P) * abs(V) * abs(M) in double: the shader multiplies it by the
		// local magnitude of each instance box to bound float rounding, which
		// grows with large cancelling world translations.
		double bound[4][4]; // [column][row]
		for (uint32_t column = 0; column < 4; column++) {
			for (uint32_t row = 0; row < 4; row++) {
				bound[column][row] = Math::abs(double(model[column][row]));
			}
		}
		for (const Projection *matrix : { &view, &projection }) {
			double next[4][4] = {};
			for (uint32_t column = 0; column < 4; column++) {
				for (uint32_t row = 0; row < 4; row++) {
					for (uint32_t k = 0; k < 4; k++) {
						next[column][row] += Math::abs(double((*matrix)[k][row])) * bound[column][k];
					}
				}
			}
			memcpy(bound, next, sizeof(bound));
		}
		for (uint32_t column = 0; column < 4; column++) {
			for (uint32_t row = 0; row < 4; row++) {
				input.abs_to_clip[column * 4 + row] = float(bound[column][row]);
			}
		}
		input.params[0] = size.width;
		input.params[1] = size.height;
		input.params[2] = 0;
		input.params[3] = 0;
	}
	if (!ensure_buffer(cull.occlusion_inputs, cull.occlusion_input_capacity, inputs.size(), sizeof(MultiMeshOcclusionInput))) {
		return false;
	}
	// The occluded lists mirror the frustum lists' layout and capacity.
	if (cull.occluded_id_capacity != cull.id_capacity) {
		if (cull.occluded_ids.is_valid()) {
			rd->free_rid(cull.occluded_ids);
		}
		cull.occluded_ids = rd->storage_buffer_create(cull.id_capacity * sizeof(uint32_t));
		cull.occluded_id_capacity = cull.occluded_ids.is_valid() ? cull.id_capacity : 0;
	}
	if (cull.occluded_draw_capacity != cull.draw_capacity) {
		if (cull.occluded_commands.is_valid()) {
			rd->free_rid(cull.occluded_commands);
		}
		cull.occluded_commands = rd->storage_buffer_create(cull.draw_capacity * 4 * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		cull.occluded_draw_capacity = cull.occluded_commands.is_valid() ? cull.draw_capacity : 0;
	}
	if (cull.occluded_ids.is_null() || cull.occluded_commands.is_null()) {
		return false;
	}

	// Visibility history: one word per instance of each transform buffer
	// (surfaces of one MultiMesh share it). The previous frame's offsets are
	// valid only for transform buffers it had with the same instance count.
	HashMap<RID, Vector2i> layout;
	uint32_t history_words = 0;
	for (const auto &draw : cull.draws) {
		if (!layout.has(draw.transforms)) {
			layout.insert(draw.transforms, Vector2i(history_words, draw.count));
			history_words += draw.count;
		}
	}
	if (cull.history_capacity < history_words) {
		for (RID &buffer : cull.history) {
			if (buffer.is_valid()) {
				rd->free_rid(buffer);
			}
		}
		cull.history_capacity = Math::next_power_of_2(MAX(history_words, 1u));
		for (RID &buffer : cull.history) {
			buffer = rd->storage_buffer_create(cull.history_capacity * sizeof(uint32_t));
		}
		if (cull.history[0].is_null() || cull.history[1].is_null()) {
			cull.history_capacity = 0;
			return false;
		}
		cull.history_layout.clear(); // The new buffers hold no history.
	}
	cull.history_write ^= 1u;
	const bool two_pass = GLOBAL_GET_CACHED(bool, "rendering/geometry/multimesh_gpu_culling/occlusion_two_pass");
	constexpr uint32_t NO_HISTORY = UINT32_MAX;
	for (uint32_t draw = 0; draw < cull.draws.size(); draw++) {
		const Vector2i *previous = cull.history_layout.getptr(cull.draws[draw].transforms);
		inputs[draw].params[2] = (two_pass && previous && uint32_t(previous->y) == cull.draws[draw].count) ? uint32_t(previous->x) : NO_HISTORY;
		inputs[draw].params[3] = uint32_t(layout[cull.draws[draw].transforms].x);
	}
	cull.history_layout = layout;

	cull.two_pass = false;
	if (two_pass) {
		// Phase lists mirror the frustum lists' layout and capacity.
		if (cull.phase_id_capacity != cull.id_capacity || cull.phase_draw_capacity != cull.draw_capacity) {
			for (RID *buffer : { &cull.phase1_ids, &cull.phase1_commands, &cull.phase2_ids, &cull.phase2_commands }) {
				if (buffer->is_valid()) {
					rd->free_rid(*buffer);
				}
			}
			cull.phase1_ids = rd->storage_buffer_create(cull.id_capacity * sizeof(uint32_t));
			cull.phase2_ids = rd->storage_buffer_create(cull.id_capacity * sizeof(uint32_t));
			cull.phase1_commands = rd->storage_buffer_create(cull.draw_capacity * 4 * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
			cull.phase2_commands = rd->storage_buffer_create(cull.draw_capacity * 4 * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
			cull.phase_id_capacity = cull.id_capacity;
			cull.phase_draw_capacity = cull.draw_capacity;
		}
		cull.two_pass = cull.phase1_ids.is_valid() && cull.phase2_ids.is_valid() && cull.phase1_commands.is_valid() && cull.phase2_commands.is_valid();
		if (!cull.two_pass) {
			cull.phase_id_capacity = 0;
			cull.phase_draw_capacity = 0;
		}
	}

	rd->buffer_update(cull.occlusion_inputs, 0, inputs.size() * sizeof(MultiMeshOcclusionInput), inputs.ptr());
	// Index counts and the other command words; the scan rewrites instance
	// counts. Uploaded, not copied from `commands` on the GPU: on D3D12 a
	// buffer copy followed by compute writes to the destination fails the
	// command list (Close returns E_INVALIDARG, then the device is removed).
	rd->buffer_update(cull.occluded_commands, 0, cull.command_words.size() * sizeof(uint32_t), cull.command_words.ptr());
	if (cull.two_pass) {
		rd->buffer_update(cull.phase1_commands, 0, cull.command_words.size() * sizeof(uint32_t), cull.command_words.ptr());
		rd->buffer_update(cull.phase2_commands, 0, cull.command_words.size() * sizeof(uint32_t), cull.command_words.ptr());
	}
	cull.occlusion_ready = true;
	return true;
}

void RenderForwardClustered::_dispatch_multimesh_selection(RD::ComputeListID p_compute, RenderBufferDataForwardClustered *p_buffers, uint32_t p_rank_mode, RID p_ids, RID p_commands, const RD::Uniform *p_extra, uint32_t p_extra_count) {
	RD *rd = RD::get_singleton();
	const auto &cull = p_buffers->multimesh_cull;
	LocalVector<RD::Uniform> uniforms;
	const RID resources[6] = { cull.inputs, p_ids, p_commands, cull.ranks, cull.blocks, p_buffers->geometry_statistics.buffer };
	for (uint32_t binding = 0; binding < 6; binding++) {
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, binding, resources[binding]));
	}
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 10, cull.group_draws));
	for (uint32_t i = 0; i < p_extra_count; i++) {
		uniforms.push_back(p_extra[i]);
	}
	const uint32_t modes[3] = { p_rank_mode, 4, 2 };
	for (uint32_t step = 0; step < 3; step++) {
		const uint32_t mode = modes[step];
		const RID shader = multimesh_cull.shader.version_get_shader(multimesh_cull.version, mode);
		rd->compute_list_bind_compute_pipeline(p_compute, multimesh_cull.pipelines[mode]);
		rd->compute_list_bind_uniform_set(p_compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		// Every mode here is batched: the scan per draw, the others per workgroup.
		_dispatch_multimesh_batched(p_compute, mode == 4 ? cull.draws.size() : cull.group_count);
		if (step != 2) {
			rd->compute_list_add_barrier(p_compute);
		}
	}
}

void RenderForwardClustered::_split_multimesh_occlusion(RenderBufferDataForwardClustered *p_buffers) {
	auto &cull = p_buffers->multimesh_cull;
	if (!cull.occlusion_ready || !cull.two_pass) {
		return;
	}
	RD *rd = RD::get_singleton();
	const RD::Uniform extra[3] = {
		RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, cull.occlusion_inputs),
		RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, cull.history[cull.history_write ^ 1u]),
		RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 9, cull.history[cull.history_write]),
	};
	rd->capture_timestamp("MultiMesh Phase 1 Split");
	RD::ComputeListID compute = rd->compute_list_begin();
	_dispatch_multimesh_selection(compute, p_buffers, 5, cull.phase1_ids, cull.phase1_commands, extra, 3);
	rd->compute_list_end();
	rd->capture_timestamp("MultiMesh Phase 1 Split End");
}

void RenderForwardClustered::_occlude_multimesh(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &cull = p_buffers->multimesh_cull;
	if (!cull.occlusion_ready || !p_buffers->hzb.ready) {
		return;
	}
	RD *rd = RD::get_singleton();
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	rd->capture_timestamp("MultiMesh HZB Occlusion");
	LocalVector<RD::Uniform> shared;
	const RID resources[6] = { cull.inputs, cull.occluded_ids, cull.occluded_commands, cull.ranks, cull.blocks, p_buffers->geometry_statistics.buffer };
	for (uint32_t binding = 0; binding < 6; binding++) {
		shared.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, binding, resources[binding]));
	}
	shared.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 10, cull.group_draws));
	LocalVector<RD::Uniform> occlude(shared);
	occlude.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, cull.occlusion_inputs));
	occlude.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, cull.history[cull.history_write ^ 1u]));
	occlude.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 9, cull.history[cull.history_write]));
	RD::Uniform pyramid;
	pyramid.binding = 7;
	pyramid.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	pyramid.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
	pyramid.append_id(rb->get_texture_slice(hzb_scope(), hzb_name(), 0, 0, 1, p_buffers->hzb.mipmaps));
	occlude.push_back(pyramid);
	// Test (re-ranks the frustum-visible instances), scan without the frustum
	// statistics, scatter into the occluded ID list.
	const uint32_t modes[3] = { 3, 4, 2 };
	RD::ComputeListID compute = rd->compute_list_begin();
	for (uint32_t step = 0; step < 3; step++) {
		const uint32_t mode = modes[step];
		const RID shader = multimesh_cull.shader.version_get_shader(multimesh_cull.version, mode);
		rd->compute_list_bind_compute_pipeline(compute, multimesh_cull.pipelines[mode]);
		rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, mode == 3 ? occlude : shared), 0);
		if (mode == 4) {
			_dispatch_multimesh_batched(compute, cull.draws.size());
		} else if (mode == 2) {
			_dispatch_multimesh_batched(compute, cull.group_count);
		} else {
			RID previous_transforms;
			for (uint32_t draw = 0; draw < cull.draws.size(); draw++) {
				if (cull.draws[draw].transforms != previous_transforms) {
					RD::Uniform transforms(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, cull.draws[draw].transforms);
					rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache(shader, 1, transforms), 1);
					previous_transforms = cull.draws[draw].transforms;
				}
				rd->compute_list_set_push_constant(compute, &draw, sizeof(draw));
				rd->compute_list_dispatch(compute, (cull.draws[draw].count + 63) / 64, 1, 1);
			}
		}
		if (step != 2) {
			rd->compute_list_add_barrier(compute);
		}
	}
	if (cull.two_pass) {
		// Phase 2: the visible instances phase 1 did not draw. Ranks and
		// blocks are rewritten, so wait for the scatter above.
		rd->compute_list_add_barrier(compute);
		const RD::Uniform extra[3] = {
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, cull.occlusion_inputs),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, cull.history[cull.history_write ^ 1u]),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 9, cull.history[cull.history_write]),
		};
		_dispatch_multimesh_selection(compute, p_buffers, 6, cull.phase2_ids, cull.phase2_commands, extra, 3);
	}
	rd->compute_list_end();
	rd->capture_timestamp("MultiMesh HZB Occlusion End");
}

void RenderForwardClustered::_occlude_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &buffers = p_buffers->mesh_clod;
	if (!buffers.occlusion_ready || !p_buffers->hzb.ready) {
		return;
	}
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	RD *rd = RD::get_singleton();
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID shader = mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 0);
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, mesh_hiz.occlude_pipeline);
	for (uint32_t draw = 0; draw < buffers.occlusion_work.size(); draw++) {
		LocalVector<RD::Uniform> uniforms;
		// Without two passes, nothing is written to the phase-2 stream; bind the colour stream in its place.
		const RID resources[7] = { buffers.occlusion_work[draw].groups, buffers.occlusion_inputs, buffers.selection, buffers.history,
			buffers.two_pass ? buffers.phase2_commands : buffers.color_commands, buffers.color_commands, buffers.counts };
		const uint32_t bindings[7] = { 0, 1, 2, 5, 6, 7, 8 };
		for (uint32_t i = 0; i < 7; i++) {
			RD::Uniform uniform;
			uniform.binding = bindings[i];
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.append_id(resources[i]);
			uniforms.push_back(uniform);
		}
		RD::Uniform pyramid;
		pyramid.binding = 3;
		pyramid.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		pyramid.append_id(sampler);
		pyramid.append_id(rb->get_texture_slice(hzb_scope(), hzb_name(), 0, 0, 1, p_buffers->hzb.mipmaps));
		uniforms.push_back(pyramid);
		RD::Uniform statistics;
		statistics.binding = 4;
		statistics.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		statistics.append_id(p_buffers->geometry_statistics.buffer);
		uniforms.push_back(statistics);
		rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		const uint32_t push[4] = { draw, buffers.two_pass ? 1u : 0u, buffers.compact ? 1u : 0u, buffers.draw_capacity };
		rd->compute_list_set_push_constant(compute, push, sizeof(push));
		rd->compute_list_dispatch(compute, (buffers.occlusion_work[draw].count + 63) / 64, 1, 1);
	}
	rd->compute_list_end();
}

namespace {
// Matches Occlusion in gpu_instance_cull.glsl (MODE_OCCLUDE).
struct GPUInstanceOcclusionInput {
	float world_to_clip[16];
	float abs_to_clip[16];
	uint32_t size[4];
};
static_assert(sizeof(GPUInstanceOcclusionInput) == 144);
} // namespace

bool RenderForwardClustered::_setup_gpu_instances_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	GPUInstanceScenario *scenario = gpu_instances_view;
	if (!scenario || !p_buffers->hzb.ready || gpu_instance_cull.occlude_pipeline.is_null() || gpu_instances_view_params[0] == 0 ||
			!GLOBAL_GET_CACHED(bool, "rendering/geometry/gpu_instances/occlusion")) {
		return false;
	}
	// As _setup_hzb() for the CPU list: a member whose colour pass writes
	// fragment depth or stencil may be nearer than its box.
	for (const GPUInstanceBucket &bucket : scenario->buckets) {
		if (bucket.surface && (bucket.surface->shader->writes_fragment_depth || bucket.surface->shader->stencil_enabled)) {
			return false;
		}
	}
	RD *rd = RD::get_singleton();
	// The colour passes' commands and IDs mirror the camera run's layout.
	if (scenario->occluded_command_capacity != scenario->command_capacity || scenario->occluded_commands_buffer.is_null()) {
		if (scenario->occluded_commands_buffer.is_valid()) {
			rd->free_rid(scenario->occluded_commands_buffer);
		}
		scenario->occluded_commands_buffer = rd->storage_buffer_create(scenario->command_capacity * 5 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		scenario->occluded_command_capacity = scenario->occluded_commands_buffer.is_valid() ? scenario->command_capacity : 0;
	}
	if (scenario->occluded_id_capacity != scenario->id_capacity || scenario->occluded_ids_buffer.is_null()) {
		if (scenario->occluded_ids_buffer.is_valid()) {
			rd->free_rid(scenario->occluded_ids_buffer);
		}
		scenario->occluded_ids_buffer = rd->storage_buffer_create(scenario->id_capacity * sizeof(uint32_t));
		scenario->occluded_id_capacity = scenario->occluded_ids_buffer.is_valid() ? scenario->id_capacity : 0;
	}
	if (scenario->occlusion_buffer.is_null()) {
		scenario->occlusion_buffer = rd->storage_buffer_create(sizeof(GPUInstanceOcclusionInput));
	}
	if (scenario->occluded_commands_buffer.is_null() || scenario->occluded_ids_buffer.is_null() || scenario->occlusion_buffer.is_null()) {
		return false;
	}
	const Size2i size = p_render_data->render_buffers->get_internal_size();
	const Projection projection = p_render_data->scene_data->get_cam_projection();
	const Projection view(p_render_data->scene_data->cam_transform.affine_inverse());
	GPUInstanceOcclusionInput input = {};
	RendererRD::MaterialStorage::store_camera(projection * view, input.world_to_clip);
	// abs(P) * abs(V) in double: the shader multiplies it by the world
	// magnitude of each box to bound float rounding (as the MultiMesh test).
	double bound[4][4]; // [column][row]
	for (uint32_t column = 0; column < 4; column++) {
		for (uint32_t row = 0; row < 4; row++) {
			bound[column][row] = Math::abs(double(view[column][row]));
		}
	}
	double next[4][4] = {};
	for (uint32_t column = 0; column < 4; column++) {
		for (uint32_t row = 0; row < 4; row++) {
			for (uint32_t k = 0; k < 4; k++) {
				next[column][row] += Math::abs(double(projection[k][row])) * bound[column][k];
			}
		}
	}
	for (uint32_t column = 0; column < 4; column++) {
		for (uint32_t row = 0; row < 4; row++) {
			input.abs_to_clip[column * 4 + row] = float(next[column][row]);
		}
	}
	input.size[0] = size.width;
	input.size[1] = size.height;
	rd->buffer_update(scenario->occlusion_buffer, 0, sizeof(input), &input);
	// Zeroed on the GPU (a clear, not a copy: see _setup_multimesh_occlusion());
	// the occlusion run writes the index count of each command it fills.
	rd->buffer_clear(scenario->occluded_commands_buffer, 0, scenario->commands.size() * sizeof(uint32_t));
	if (scenario->two_pass) {
		rd->buffer_clear(scenario->phase2_commands_buffer, 0, scenario->commands.size() * sizeof(uint32_t));
	}
	return true;
}

void RenderForwardClustered::_occlude_gpu_instances(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	GPUInstanceScenario *scenario = gpu_instances_view;
	if (!scenario || !p_buffers->hzb.ready) {
		return;
	}
	RD *rd = RD::get_singleton();
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	const RID shader = gpu_instance_cull.shader.version_get_shader(gpu_instance_cull.version, 1);
	const RID resources[8] = { gpu_instance_table.get_buffer(), scenario->members_buffer, scenario->buckets_buffer, scenario->occluded_commands_buffer, scenario->occluded_ids_buffer,
		scenario->lod_edges_buffer, scenario->visible_buffer, scenario->occlusion_buffer };
	const uint32_t bindings[8] = { 0, 1, 2, 3, 5, 6, 9, 10 };
	LocalVector<RD::Uniform> uniforms;
	for (uint32_t i = 0; i < 8; i++) {
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, bindings[i], resources[i]));
	}
	RD::Uniform pyramid;
	pyramid.binding = 11;
	pyramid.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	pyramid.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
	pyramid.append_id(rb->get_texture_slice(hzb_scope(), hzb_name(), 0, 0, 1, p_buffers->hzb.mipmaps));
	uniforms.push_back(pyramid);
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 12, p_buffers->geometry_statistics.buffer));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 13, scenario->command_indices_buffer));
	_gpu_instances_bind_two_pass(*scenario, scenario->phase2_commands_buffer, scenario->phase2_ids_buffer, uniforms);
	rd->capture_timestamp("GPU Instances HZB Occlusion");
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, gpu_instance_cull.occlude_pipeline);
	rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
	rd->compute_list_set_push_constant(compute, gpu_instances_view_params, sizeof(gpu_instances_view_params));
	rd->compute_list_dispatch(compute, (gpu_instances_view_params[0] + 63) / 64, 1, 1);
	rd->compute_list_end();
	rd->capture_timestamp("GPU Instances HZB Occlusion End");
}

namespace {
// Matches Element and View in mesh_occlusion.glsl.
struct MeshOcclusionElement {
	float position[4];
	float size[4];
	uint32_t draw[4]; // Command index (= opaque list element), index or vertex count, instances, depth pass index or vertex count.
	uint32_t slots[4]; // First entry in the slot list, number of entries, unused.
};
static_assert(sizeof(MeshOcclusionElement) == 64);
struct MeshOcclusionView {
	float world_to_clip[16];
	float abs_to_clip[16];
	uint32_t size[4];
};
static_assert(sizeof(MeshOcclusionView) == 144);
} // namespace

bool RenderForwardClustered::_setup_mesh_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers, bool p_depth_shadow_mesh) {
	auto &occlusion = p_buffers->mesh_occlusion;
	occlusion.ready = false;
	occlusion.two_pass = false;
	occlusion.count = 0;
	const RSE::ViewportDebugDraw debug_draw = get_debug_draw_mode();
	// Wireframe draws other index arrays; overdraw should show every layer.
	if (!p_buffers->hzb.ready || p_render_data->scene_data->view_count != 1 || debug_draw == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME || debug_draw == RSE::VIEWPORT_DEBUG_DRAW_OVERDRAW ||
			!GLOBAL_GET_CACHED(bool, "rendering/geometry/mesh_occlusion/enabled")) {
		return false;
	}
	RD *rd = RD::get_singleton();
	if (mesh_hiz.mesh_occlusion_version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		modes.push_back("\n#define MODE_SPLIT\n");
		mesh_hiz.mesh_occlusion_shader.initialize(modes);
		mesh_hiz.mesh_occlusion_version = mesh_hiz.mesh_occlusion_shader.version_create();
		for (uint32_t mode = 0; mode < 2; mode++) {
			mesh_hiz.mesh_occlusion_pipelines[mode] = rd->compute_pipeline_create(mesh_hiz.mesh_occlusion_shader.version_get_shader(mesh_hiz.mesh_occlusion_version, mode));
		}
	}
	for (RID pipeline : mesh_hiz.mesh_occlusion_pipelines) {
		if (pipeline.is_null()) {
			return false;
		}
	}

	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	const uint32_t list_size = list.elements.size();
	occlusion.tested.resize(list_size);
	if (list_size > 0) {
		memset(occlusion.tested.ptr(), 0, list_size);
	}
	LocalVector<MeshOcclusionElement> elements;
	LocalVector<uint32_t> slots;
	uint32_t slot_end = 0;
	for (uint32_t i = 0; i < list_size; i++) {
		const RenderElementInfo &info = list.element_info[i];
		const GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		const uint32_t repeat = MAX(info.repeat, 1u);
		const GeometryInstanceForwardClustered *instance = surface->owner;
		const SceneShaderForwardClustered::ShaderData *shader = surface->shader;
		// Drawn by the GPU paths, by indirect or instanced commands of their
		// own, or with vertices the box may not bound: keep the direct draw.
		const bool skip = info.gpu_bucket != UINT32_MAX || info.mesh_clod_offset != UINT32_MAX || instance->instance_count == 0 ||
				(instance->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES)) || surface->primitive == RSE::PRIMITIVE_POINTS ||
				!shader || shader->uses_vertex || shader->uses_position || shader->writes_modelview_or_projection || shader->uses_custom_vertex_transform ||
				shader->uses_point_size || shader->uses_particle_trails;
		if (!skip) {
			// One box for the instances the element draws together.
			AABB box = instance->transformed_aabb;
			for (uint32_t r = 1; r < repeat && i + r < list_size; r++) {
				box.merge_with(list.elements[i + r]->owner->transformed_aabb);
			}
			// The draw arguments of a mesh surface at the element's LOD, as
			// the render list binds them.
			auto draw_count = [&](void *p_surface) -> uint32_t {
				const bool indexed = mesh_storage->mesh_surface_get_index_array(p_surface, info.lod_index).is_valid();
				return indexed && info.lod_index > 0 ? mesh_storage->mesh_surface_get_index_count(p_surface, info.lod_index) : mesh_storage->mesh_surface_get_vertices_drawn_count(p_surface);
			};
			const uint32_t count = draw_count(surface->surface);
			// PASS_MODE_DEPTH draws the shadow mesh, which may have other counts.
			const uint32_t depth_count = p_depth_shadow_mesh ? draw_count(surface->surface_shadow) : count;
			if (box.is_finite() && count > 0 && depth_count > 0) {
				MeshOcclusionElement element = {};
				element.position[0] = box.position.x;
				element.position[1] = box.position.y;
				element.position[2] = box.position.z;
				element.size[0] = box.size.x;
				element.size[1] = box.size.y;
				element.size[2] = box.size.z;
				element.draw[0] = i;
				element.draw[1] = count;
				element.draw[2] = repeat;
				element.draw[3] = depth_count;
				element.slots[0] = slots.size();
				element.slots[1] = repeat;
				for (uint32_t r = 0; r < repeat; r++) {
					const uint32_t slot = list.elements[MIN(i + r, list_size - 1)]->owner->gpu_slot;
					slots.push_back(slot);
					slot_end = MAX(slot_end, slot + 1);
				}
				elements.push_back(element);
				occlusion.tested[i] = 1;
			}
		}
		i += repeat - 1;
	}
	if (elements.is_empty()) {
		return false;
	}
	if (!ensure_buffer(occlusion.elements, occlusion.element_capacity, elements.size(), sizeof(MeshOcclusionElement)) ||
			!ensure_buffer(occlusion.commands, occlusion.command_capacity, list_size, 5 * sizeof(uint32_t), RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT) ||
			!ensure_buffer(occlusion.phase1_commands, occlusion.phase1_capacity, list_size, 5 * sizeof(uint32_t), RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT) ||
			!ensure_buffer(occlusion.phase2_commands, occlusion.phase2_capacity, list_size, 5 * sizeof(uint32_t), RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT) ||
			!ensure_buffer(occlusion.slots, occlusion.slot_capacity, slots.size(), sizeof(uint32_t))) {
		return false;
	}
	if (occlusion.history_capacity < slot_end) {
		// Grown empty: every slot counts as visible, so phase 1 draws it.
		const uint32_t old_capacity = occlusion.history_capacity;
		if (!ensure_buffer(occlusion.history, occlusion.history_capacity, slot_end, sizeof(uint32_t))) {
			return false;
		}
		if (occlusion.history_capacity != old_capacity) {
			rd->buffer_clear(occlusion.history, 0, occlusion.history_capacity * sizeof(uint32_t));
		}
	}
	if (occlusion.view.is_null()) {
		occlusion.view = rd->storage_buffer_create(sizeof(MeshOcclusionView));
		if (occlusion.view.is_null()) {
			return false;
		}
	}
	const Size2i size = p_render_data->render_buffers->get_internal_size();
	const Projection projection = p_render_data->scene_data->get_cam_projection();
	const Projection view_matrix(p_render_data->scene_data->cam_transform.affine_inverse());
	MeshOcclusionView view = {};
	RendererRD::MaterialStorage::store_camera(projection * view_matrix, view.world_to_clip);
	// abs(P) * abs(V) in double: the shader multiplies it by the world
	// magnitude of each box to bound float rounding (as the GPU instance test).
	for (uint32_t column = 0; column < 4; column++) {
		for (uint32_t row = 0; row < 4; row++) {
			double sum = 0.0;
			for (uint32_t k = 0; k < 4; k++) {
				sum += Math::abs(double(projection[k][row])) * Math::abs(double(view_matrix[column][k]));
			}
			view.abs_to_clip[column * 4 + row] = float(sum);
		}
	}
	view.size[0] = size.width;
	view.size[1] = size.height;
	rd->buffer_update(occlusion.view, 0, sizeof(view), &view);
	rd->buffer_update(occlusion.elements, 0, elements.size() * sizeof(MeshOcclusionElement), elements.ptr());
	rd->buffer_update(occlusion.slots, 0, slots.size() * sizeof(uint32_t), slots.ptr());
	occlusion.count = elements.size();
	occlusion.ready = true;
	occlusion.two_pass = GLOBAL_GET_CACHED(bool, "rendering/geometry/mesh_occlusion/two_pass");
	return true;
}

void RenderForwardClustered::_dispatch_mesh_occlusion(RenderBufferDataForwardClustered *p_buffers, uint32_t p_mode, const RD::Uniform *p_pyramid) {
	auto &occlusion = p_buffers->mesh_occlusion;
	RD *rd = RD::get_singleton();
	const RID shader = mesh_hiz.mesh_occlusion_shader.version_get_shader(mesh_hiz.mesh_occlusion_version, p_mode);
	LocalVector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, occlusion.elements));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, occlusion.view));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 4, p_buffers->geometry_statistics.buffer));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 5, occlusion.slots));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, occlusion.history));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 7, occlusion.phase1_commands));
	if (p_pyramid) {
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, occlusion.commands));
		uniforms.push_back(*p_pyramid);
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 8, occlusion.phase2_commands));
	}
	const uint32_t push[4] = { occlusion.count, occlusion.two_pass ? 1u : 0u, 0, 0 };
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, mesh_hiz.mesh_occlusion_pipelines[p_mode]);
	rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
	rd->compute_list_set_push_constant(compute, push, sizeof(push));
	rd->compute_list_dispatch(compute, (occlusion.count + 63) / 64, 1, 1);
	rd->compute_list_end();
}

void RenderForwardClustered::_split_mesh_occlusion(RenderBufferDataForwardClustered *p_buffers) {
	if (!p_buffers->mesh_occlusion.ready || !p_buffers->mesh_occlusion.two_pass) {
		return;
	}
	RD::get_singleton()->capture_timestamp("Mesh Phase 1 Split");
	_dispatch_mesh_occlusion(p_buffers, 1, nullptr);
}

void RenderForwardClustered::_occlude_meshes(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &occlusion = p_buffers->mesh_occlusion;
	if (!occlusion.ready || !p_buffers->hzb.ready) {
		return;
	}
	RD *rd = RD::get_singleton();
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	RD::Uniform pyramid;
	pyramid.binding = 3;
	pyramid.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	pyramid.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
	pyramid.append_id(rb->get_texture_slice(hzb_scope(), hzb_name(), 0, 0, 1, p_buffers->hzb.mipmaps));
	rd->capture_timestamp("Mesh HZB Occlusion");
	_dispatch_mesh_occlusion(p_buffers, 0, &pyramid);
	rd->capture_timestamp("Mesh HZB Occlusion End");
}
