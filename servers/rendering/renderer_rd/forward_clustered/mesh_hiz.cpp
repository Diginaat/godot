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
	// Pass commands mirror the selection command buffer, word for word.
	const uint32_t command_words = buffers.group_capacity * 5;
	if (buffers.pass_command_capacity != command_words) {
		for (RID *buffer : { &buffers.depth_commands, &buffers.phase2_commands }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
			*buffer = rd->storage_buffer_create(command_words * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		}
		buffers.pass_command_capacity = command_words;
	}
	if (buffers.depth_commands.is_null() || buffers.phase2_commands.is_null() || groups_total > buffers.group_capacity) {
		buffers.pass_command_capacity = 0;
		return false;
	}
	rd->buffer_update(buffers.occlusion_inputs, 0, inputs.size() * sizeof(OcclusionInput), inputs.ptr());
	buffers.occlusion_ready = true;
	buffers.two_pass = GLOBAL_GET("rendering/geometry/cluster_lod/occlusion_two_pass");
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
		const RID resources[5] = { buffers.occlusion_work[draw].groups, buffers.occlusion_inputs, buffers.commands, buffers.history, buffers.depth_commands };
		const uint32_t bindings[5] = { 0, 1, 2, 5, 6 };
		for (uint32_t i = 0; i < 5; i++) {
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
		const uint32_t push[4] = { draw, 1, 0, 0 };
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
	rd->buffer_update(cull.occlusion_inputs, 0, inputs.size() * sizeof(MultiMeshOcclusionInput), inputs.ptr());
	// Index counts and the other command words; the scan rewrites instance
	// counts. Uploaded, not copied from `commands` on the GPU: on D3D12 a
	// buffer copy followed by compute writes to the destination fails the
	// command list (Close returns E_INVALIDARG, then the device is removed).
	rd->buffer_update(cull.occluded_commands, 0, cull.command_words.size() * sizeof(uint32_t), cull.command_words.ptr());
	cull.occlusion_ready = true;
	return true;
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
	LocalVector<RD::Uniform> occlude(shared);
	occlude.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, cull.occlusion_inputs));
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
		RID previous_transforms;
		for (uint32_t draw = 0; draw < cull.draws.size(); draw++) {
			if (mode == 3 && cull.draws[draw].transforms != previous_transforms) {
				RD::Uniform transforms(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, cull.draws[draw].transforms);
				rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache(shader, 1, transforms), 1);
				previous_transforms = cull.draws[draw].transforms;
			}
			rd->compute_list_set_push_constant(compute, &draw, sizeof(draw));
			rd->compute_list_dispatch(compute, mode == 4 ? 1 : (cull.draws[draw].count + 63) / 64, 1, 1);
		}
		if (step != 2) {
			rd->compute_list_add_barrier(compute);
		}
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
		const RID resources[5] = { buffers.occlusion_work[draw].groups, buffers.occlusion_inputs, buffers.commands, buffers.history, buffers.phase2_commands };
		const uint32_t bindings[5] = { 0, 1, 2, 5, 6 };
		for (uint32_t i = 0; i < 5; i++) {
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
		const uint32_t push[4] = { draw, buffers.two_pass ? 1u : 0u, 0, 0 };
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
	// Zero instances in every command; uploaded, not copied on the GPU (see
	// _setup_multimesh_occlusion()).
	rd->buffer_update(scenario->occluded_commands_buffer, 0, scenario->commands.size() * sizeof(uint32_t), scenario->commands.ptr());
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
	rd->capture_timestamp("GPU Instances HZB Occlusion");
	RD::ComputeListID compute = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute, gpu_instance_cull.occlude_pipeline);
	rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
	rd->compute_list_set_push_constant(compute, gpu_instances_view_params, sizeof(gpu_instances_view_params));
	rd->compute_list_dispatch(compute, (gpu_instances_view_params[0] + 63) / 64, 1, 1);
	rd->compute_list_end();
	rd->capture_timestamp("GPU Instances HZB Occlusion End");
}
