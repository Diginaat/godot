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

void RenderForwardClustered::_occlude_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers) {
	auto &buffers = p_buffers->mesh_clod;
	const Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	if (!buffers.active || p_buffers->geometry_statistics.buffer.is_null() || !bool(GLOBAL_GET("rendering/geometry/cluster_lod/occlusion_enabled")) ||
			rb->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED || p_render_data->compositor.is_valid() || p_render_data->render_region.has_area() ||
			// Group rectangles use the unjittered projection; a sub-pixel shift
			// could reject a group whose jittered footprint is still visible.
			p_render_data->scene_data->taa_jitter != Vector2()) {
		return;
	}
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		// Custom fragment depth can invalidate geometric nearest-depth bounds,
		// including when the material is used only as an occluder, not a CLOD mesh.
		if (list.elements[i]->shader->writes_fragment_depth || list.elements[i]->shader->stencil_enabled) {
			return;
		}
	}
	const Size2i size = rb->get_internal_size();
	const Size2i padded(Math::next_power_of_2(uint32_t(size.width)), Math::next_power_of_2(uint32_t(size.height)));
	// Twice the base image bounds the full mip chain even for 1-pixel strips.
	if (uint64_t(padded.width) * padded.height * 8 > 64 * 1024 * 1024) {
		return;
	}
	RD *rd = RD::get_singleton();
	const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	if (!rd->texture_is_format_supported_for_usage(RD::DATA_FORMAT_R32_SFLOAT, usage)) {
		return;
	}
	struct Input {
		float local_to_clip[16];
		float clip_error[4];
		uint32_t params[4];
	};
	static_assert(sizeof(Input) == 96);
	struct Work {
		RID groups;
		uint32_t count;
	};
	Vector<Input> inputs;
	Vector<Work> work;
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	const Projection projection = p_render_data->scene_data->get_cam_projection();
	const Projection view(p_render_data->scene_data->cam_transform.affine_inverse());
	const Projection view_projection = projection * view;
	for (uint32_t i = 0; i < list.elements.size(); i++) {
		if (list.element_info[i].mesh_clod_offset == UINT32_MAX) {
			continue;
		}
		const GeometryInstanceSurfaceDataCache *surface = list.elements[i];
		const auto &resident = storage->mesh_surface_get_cluster_data(surface->surface);
		Input input = {};
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
		inputs.push_back(input);
		work.push_back({ resident.groups, resident.group_count });
	}
	if (work.is_empty()) {
		return;
	}
	uint32_t mipmaps = 1;
	for (uint32_t extent = MAX(padded.width, padded.height); extent > 1; extent >>= 1) {
		mipmaps++;
	}
	const StringName scope = SNAME("mesh_clod_hzb");
	const StringName name = SNAME("depth");
	if (!rb->has_texture(scope, name)) {
		if (rb->create_texture(scope, name, RD::DATA_FORMAT_R32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, padded, 1, mipmaps).is_null()) {
			return;
		}
	}
	if (buffers.occlusion_capacity < uint32_t(inputs.size())) {
		if (buffers.occlusion_inputs.is_valid()) {
			rd->free_rid(buffers.occlusion_inputs);
		}
		buffers.occlusion_capacity = Math::next_power_of_2(uint32_t(inputs.size()));
		buffers.occlusion_inputs = rd->storage_buffer_create(buffers.occlusion_capacity * sizeof(Input));
		if (buffers.occlusion_inputs.is_null()) {
			buffers.occlusion_capacity = 0;
			return;
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
		mesh_hiz.occlude_shader.initialize(occlude_modes, "\n#define GEOMETRY_STATISTICS\n");
		mesh_hiz.occlude_version = mesh_hiz.occlude_shader.version_create();
		mesh_hiz.occlude_pipeline = rd->compute_pipeline_create(mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 0));
	}
	if (mesh_hiz.reduce_pipelines[0].is_null() || mesh_hiz.reduce_pipelines[1].is_null() || mesh_hiz.occlude_pipeline.is_null()) {
		return;
	}
	rd->buffer_update(buffers.occlusion_inputs, 0, inputs.size() * sizeof(Input), inputs.ptr());
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	rd->capture_timestamp("Mesh CLOD HZB");
	RD::ComputeListID compute = rd->compute_list_begin();
	for (uint32_t mip = 0; mip < mipmaps; mip++) {
		const uint32_t mode = mip == 0 ? 0 : 1;
		const RID shader = mesh_hiz.reduce_shader.version_get_shader(mesh_hiz.reduce_version, mode);
		const RID source = mip == 0 ? rb->get_depth_texture(0) : rb->get_texture_slice(scope, name, 0, mip - 1);
		const RID target = rb->get_texture_slice(scope, name, 0, mip);
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
		rd->compute_list_add_barrier(compute);
	}
	const RID shader = mesh_hiz.occlude_shader.version_get_shader(mesh_hiz.occlude_version, 0);
	rd->compute_list_bind_compute_pipeline(compute, mesh_hiz.occlude_pipeline);
	for (uint32_t draw = 0; draw < work.size(); draw++) {
		LocalVector<RD::Uniform> uniforms;
		const RID resources[3] = { work[draw].groups, buffers.occlusion_inputs, buffers.commands };
		for (uint32_t binding = 0; binding < 3; binding++) {
			RD::Uniform uniform;
			uniform.binding = binding;
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.append_id(resources[binding]);
			uniforms.push_back(uniform);
		}
		RD::Uniform pyramid;
		pyramid.binding = 3;
		pyramid.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		pyramid.append_id(sampler);
		pyramid.append_id(rb->get_texture_slice(scope, name, 0, 0, 1, mipmaps));
		uniforms.push_back(pyramid);
		RD::Uniform statistics;
		statistics.binding = 4;
		statistics.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		statistics.append_id(p_buffers->geometry_statistics.buffer);
		uniforms.push_back(statistics);
		rd->compute_list_bind_uniform_set(compute, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
		rd->compute_list_set_push_constant(compute, &draw, sizeof(draw));
		rd->compute_list_dispatch(compute, (work[draw].count + 63) / 64, 1, 1);
	}
	rd->compute_list_end();
}
