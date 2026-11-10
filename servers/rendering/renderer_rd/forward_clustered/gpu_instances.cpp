/**************************************************************************/
/*  gpu_instances.cpp                                                     */
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

// GPU path for eligible instances (docs/renderer/roadmap.md, Phase 2, D027).
// Eligible mesh instances leave the CPU camera render lists: per scenario,
// their surfaces are grouped into draw buckets, and per camera view a compute
// pass culls the bucket members against the frustum and writes the instance
// data Forward+ draws with, then each bucket is one indirect draw.

#include "render_forward_clustered.h"

#include "core/config/project_settings.h"
#include "core/math/math_funcs_binary.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_TEXTURE_STREAMING_ENABLED
#include "modules/texture_streaming/texture_streaming.h"
#endif

using namespace RendererSceneRenderImplementation;

namespace {
// Matches Params in gpu_instance_cull.glsl.
struct GPUInstanceCullParams {
	float planes[6][4];
	uint32_t member_count;
	uint32_t visible_layers;
	uint32_t view_flags;
	float lod_threshold;
	float lod_camera[3];
	float lod_distance_multiplier;
};
static_assert(sizeof(GPUInstanceCullParams) == 128);

enum {
	GPU_INSTANCE_VIEW_USE_GI_BUFFERS = 1,
	GPU_INSTANCE_VIEW_USE_SDFGI = 2,
	GPU_INSTANCE_VIEW_ORTHOGONAL = 4,
};

void gpu_instances_free_buffers(RID *const (&p_buffers)[6]) {
	RD *rd = RD::get_singleton();
	for (RID *buffer : p_buffers) {
		if (buffer->is_valid()) {
			rd->free_rid(*buffer);
			*buffer = RID();
		}
	}
}

// Render-element sort key bits that change per view or frame (LOD, forward GI,
// lightmap, depth layer); a bucket's key leaves them out.
constexpr uint64_t GPU_INSTANCE_KEY1_VIEW_BITS = 0xFCFF;
} // namespace

bool RenderForwardClustered::gpu_instances_begin_cull() {
	// Members that joined since the last cull start now, so that this cull
	// lists them apart exactly when the buckets hold them.
	for (GeometryInstanceForwardClustered *instance : gpu_instances_pending) {
		instance->gpu_pending_index = UINT32_MAX;
		instance->gpu_drawn = true;
		GPUInstanceScenario **scenario = gpu_instance_scenarios.getptr(instance->gpu_member_scenario);
		if (scenario) {
			(*scenario)->dirty = true;
		}
	}
	gpu_instances_pending.clear();
	gpu_instances_left.clear();
	return gpu_instances_active;
}

bool RenderForwardClustered::_gpu_instance_eligible(const GeometryInstanceForwardClustered *p_instance) const {
#ifdef REAL_T_IS_DOUBLE
	return false; // The compute pass writes no split-precision origins.
#else
	const GeometryInstanceForwardClustered *instance = p_instance;
	if (!instance->data || instance->data->base_type != RSE::INSTANCE_MESH || instance->gpu_slot == GPUInstanceSlots::INVALID) {
		return false;
	}
	if ((instance->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES)) || instance->instance_count != 1 || !instance->store_transform_cache) {
		return false;
	}
	// The compute pass writes neither lightmap nor VoxelGI indices, nor fade.
	if (instance->mesh_instance.is_valid() || instance->rt_procedural || instance->lightmap_instance.is_valid() || instance->lightmap_sh ||
			instance->voxel_gi_instances[0].is_valid() || instance->voxel_gi_instances[1].is_valid()) {
		return false;
	}
	if (instance->fade_near || instance->fade_far || instance->force_alpha < 1.0f || instance->parent_fade_alpha < 1.0f) {
		return false;
	}
	if (!instance->surface_caches) {
		return false;
	}
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	// Surfaces that need a pass, buffer or per-view choice the buckets do not
	// make: transparency, screen and depth reads, stencil, motion vectors,
	// cluster LOD, and shadow meshes with fewer index LODs than the mesh (the
	// depth pre-pass draws the shadow mesh at the mesh's LOD). The cull pass
	// chooses index LODs per instance.
	const uint32_t excluded = GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA | GeometryInstanceSurfaceDataCache::FLAG_USES_SUBSURFACE_SCATTERING |
			GeometryInstanceSurfaceDataCache::FLAG_USES_SCREEN_TEXTURE | GeometryInstanceSurfaceDataCache::FLAG_USES_DEPTH_TEXTURE |
			GeometryInstanceSurfaceDataCache::FLAG_USES_NORMAL_TEXTURE | GeometryInstanceSurfaceDataCache::FLAG_USES_PARTICLE_TRAILS |
			GeometryInstanceSurfaceDataCache::FLAG_USES_MOTION_VECTOR | GeometryInstanceSurfaceDataCache::FLAG_USES_STENCIL;
	for (const GeometryInstanceSurfaceDataCache *surface = instance->surface_caches; surface; surface = surface->next) {
		if (!(surface->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE)) || (surface->flags & excluded)) {
			return false;
		}
		if (!surface->surface || !surface->surface_shadow || !surface->shader || !surface->shader_shadow || !surface->material ||
				surface->primitive == RSE::PRIMITIVE_POINTS) {
			return false;
		}
		if (storage->mesh_surface_get_cluster_data(surface->surface).group_count > 0) {
			return false;
		}
		if (surface->surface_shadow != surface->surface) {
			const uint32_t lods = storage->mesh_surface_has_lod(surface->surface) ? storage->mesh_surface_get_lod_count(surface->surface) : 0;
			const uint32_t shadow_lods = storage->mesh_surface_has_lod(surface->surface_shadow) ? storage->mesh_surface_get_lod_count(surface->surface_shadow) : 0;
			if (shadow_lods < lods || storage->mesh_surface_get_cluster_data(surface->surface_shadow).group_count > 0) {
				return false;
			}
		}
	}
	return true;
#endif
}

void RenderForwardClustered::_gpu_instance_refresh(GeometryInstanceForwardClustered *p_instance, bool p_rendering) {
	if (p_instance->dirty_list_element.in_list() && p_instance->gpu_scenario != 0) {
		// Its surfaces are being rebuilt; the update refreshes it.
		if (p_instance->gpu_member_scenario != 0) {
			gpu_instance_scenarios[p_instance->gpu_member_scenario]->dirty = true;
		}
		return;
	}
	uint32_t surface_count = 0;
	for (const GeometryInstanceSurfaceDataCache *surface = p_instance->surface_caches; surface; surface = surface->next) {
		surface_count++;
	}
	p_instance->gpu_surface_count = surface_count;

	const uint64_t scenario = (gpu_instances_active && p_instance->gpu_scenario != 0 && _gpu_instance_eligible(p_instance)) ? p_instance->gpu_scenario : 0;
	if (scenario == p_instance->gpu_member_scenario) {
		if (scenario != 0) {
			gpu_instance_scenarios[scenario]->dirty = true; // Its surfaces may have changed.
		}
		return;
	}
	if (p_instance->gpu_member_scenario != 0) {
		// The cull of this render listed it for the GPU path, which no longer
		// draws it: the CPU render list takes it this time.
		const bool listed = p_rendering && p_instance->gpu_drawn && p_instance->gpu_scenario == p_instance->gpu_member_scenario;
		_gpu_instance_leave(p_instance);
		if (listed) {
			gpu_instances_left.push_back(p_instance);
		}
	}
	if (scenario == 0) {
		return;
	}
	GPUInstanceScenario **found = gpu_instance_scenarios.getptr(scenario);
	GPUInstanceScenario *target = found ? *found : nullptr;
	if (!target) {
		target = memnew(GPUInstanceScenario);
		gpu_instance_scenarios.insert(scenario, target);
	}
	p_instance->gpu_member_scenario = scenario;
	p_instance->gpu_member_index = target->instances.size();
	target->instances.push_back(p_instance);
	// Drawn by the GPU path from the next cull on (gpu_instances_begin_cull).
	p_instance->gpu_pending_index = gpu_instances_pending.size();
	gpu_instances_pending.push_back(p_instance);
}

void RenderForwardClustered::_gpu_instance_leave(GeometryInstanceForwardClustered *p_instance) {
	const uint64_t key = p_instance->gpu_member_scenario;
	GPUInstanceScenario **found = gpu_instance_scenarios.getptr(key);
	ERR_FAIL_NULL(found);
	GPUInstanceScenario *scenario = *found;
	const uint32_t index = p_instance->gpu_member_index;
	ERR_FAIL_COND(index >= scenario->instances.size() || scenario->instances[index] != p_instance);
	GeometryInstanceForwardClustered *last = scenario->instances[scenario->instances.size() - 1];
	scenario->instances[index] = last;
	last->gpu_member_index = index;
	scenario->instances.resize(scenario->instances.size() - 1);
	scenario->dirty = true;

	if (p_instance->gpu_pending_index != UINT32_MAX) {
		GeometryInstanceForwardClustered *last_pending = gpu_instances_pending[gpu_instances_pending.size() - 1];
		gpu_instances_pending[p_instance->gpu_pending_index] = last_pending;
		last_pending->gpu_pending_index = p_instance->gpu_pending_index;
		gpu_instances_pending.resize(gpu_instances_pending.size() - 1);
		p_instance->gpu_pending_index = UINT32_MAX;
	}
	p_instance->gpu_member_scenario = 0;
	p_instance->gpu_member_index = UINT32_MAX;
	p_instance->gpu_drawn = false;

	if (scenario->instances.is_empty()) {
		gpu_instances_free_buffers({ &scenario->members_buffer, &scenario->buckets_buffer, &scenario->commands_buffer, &scenario->lod_edges_buffer, &scenario->output_buffer, &scenario->ids_buffer });
		gpu_instance_scenarios.erase(key);
		memdelete(scenario);
	}
}

void RenderForwardClustered::_gpu_instances_update_setting() {
	bool wanted = GLOBAL_GET_CACHED(bool, "rendering/geometry/gpu_instances/enabled");
#ifdef REAL_T_IS_DOUBLE
	wanted = false;
#endif
	if (wanted == gpu_instances_active) {
		return;
	}
	gpu_instances_active = wanted;
	for (GeometryInstanceForwardClustered *instance : gpu_instance_owners) {
		if (instance) {
			_gpu_instance_refresh(instance, true);
		}
	}
	if (!gpu_instances_active) {
		_gpu_instances_free();
	}
}

void RenderForwardClustered::_gpu_instances_free() {
	for (KeyValue<uint64_t, GPUInstanceScenario *> &E : gpu_instance_scenarios) {
		GPUInstanceScenario *scenario = E.value;
		for (GeometryInstanceForwardClustered *instance : scenario->instances) {
			instance->gpu_member_scenario = 0;
			instance->gpu_member_index = UINT32_MAX;
			instance->gpu_pending_index = UINT32_MAX;
			instance->gpu_drawn = false;
		}
		gpu_instances_free_buffers({ &scenario->members_buffer, &scenario->buckets_buffer, &scenario->commands_buffer, &scenario->lod_edges_buffer, &scenario->output_buffer, &scenario->ids_buffer });
		memdelete(scenario);
	}
	gpu_instance_scenarios.clear();
	gpu_instances_pending.clear();
	gpu_instances_left.clear();
	gpu_instances_left_view.clear();
}

void RenderForwardClustered::_gpu_instances_rebuild(GPUInstanceScenario &p_scenario) {
	struct Entry {
		uint64_t key1;
		uint64_t key2;
		uint32_t mirror;
		uint32_t slot;
		GeometryInstanceSurfaceDataCache *surface;
		bool operator<(const Entry &p_other) const {
			// The CPU render list's order (RenderList::SortByKey), then mirror.
			if (key2 != p_other.key2) {
				return key2 < p_other.key2;
			}
			if (key1 != p_other.key1) {
				return key1 < p_other.key1;
			}
			return mirror < p_other.mirror;
		}
		bool same_draw(const Entry &p_other) const {
			return key1 == p_other.key1 && key2 == p_other.key2 && mirror == p_other.mirror;
		}
	};
	thread_local LocalVector<Entry> entries;
	entries.clear();
	for (GeometryInstanceForwardClustered *instance : p_scenario.instances) {
		if (!instance->gpu_drawn) {
			continue; // Joins at the next cull.
		}
		for (GeometryInstanceSurfaceDataCache *surface = instance->surface_caches; surface; surface = surface->next) {
			entries.push_back({ surface->sort.sort_key1 & ~GPU_INSTANCE_KEY1_VIEW_BITS, surface->sort.sort_key2, uint32_t(instance->mirror), instance->gpu_slot, surface });
		}
	}
	entries.sort();

	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	p_scenario.buckets.clear();
	p_scenario.bucket_data.clear();
	p_scenario.commands.clear();
	p_scenario.command_levels.clear();
	p_scenario.command_depth.clear();
	p_scenario.lod_edges.clear();
	p_scenario.members.resize(entries.size() * 2);
	for (uint32_t i = 0; i < entries.size(); i++) {
		if (i == 0 || !entries[i].same_draw(entries[i - 1])) {
			GPUInstanceBucket bucket;
			bucket.surface = entries[i].surface;
			bucket.first = i;
			p_scenario.buckets.push_back(bucket);
		}
		p_scenario.buckets[p_scenario.buckets.size() - 1].count++;
		p_scenario.members[i * 2 + 0] = entries[i].slot;
		p_scenario.members[i * 2 + 1] = p_scenario.buckets.size() - 1;
	}
	// Per bucket, one indexed or non-indexed command per index LOD (the kernel
	// writes word 1), and count visible IDs reserved per level.
	uint32_t id_count = 0;
	for (GPUInstanceBucket &bucket : p_scenario.buckets) {
		void *surface = bucket.surface->surface;
		const uint32_t lod_count = storage->mesh_surface_has_lod(surface) ? storage->mesh_surface_get_lod_count(surface) : 0;
		bucket.levels = lod_count + 1;
		bucket.command = p_scenario.commands.size() / 5;
		bucket.ids_first = id_count;
		id_count += bucket.count * bucket.levels;
		for (uint32_t level = 0; level < bucket.levels; level++) {
			p_scenario.commands.push_back(level == 0 ? storage->mesh_surface_get_vertices_drawn_count(surface) : storage->mesh_surface_get_index_count(surface, level));
			for (uint32_t word = 1; word < 5; word++) {
				p_scenario.commands.push_back(0);
			}
			p_scenario.command_levels.push_back(uint8_t(MIN(level, 7u)));
			p_scenario.command_depth.push_back(UINT32_MAX);
		}
		bucket.depth_command = UINT32_MAX;
		void *shadow = bucket.surface->surface_shadow;
		if (shadow != surface) {
			// The same instance counts, with the shadow mesh's index counts.
			bucket.depth_command = p_scenario.commands.size() / 5;
			for (uint32_t level = 0; level < bucket.levels; level++) {
				p_scenario.commands.push_back(level == 0 ? storage->mesh_surface_get_vertices_drawn_count(shadow) : storage->mesh_surface_get_index_count(shadow, level));
				for (uint32_t word = 1; word < 5; word++) {
					p_scenario.commands.push_back(0);
				}
				p_scenario.command_levels.push_back(0);
				p_scenario.command_depth.push_back(UINT32_MAX - 1);
				p_scenario.command_depth[bucket.command + level] = bucket.depth_command + level;
			}
		}
		for (uint32_t lod = 0; lod < lod_count; lod++) {
			p_scenario.lod_edges.push_back(storage->mesh_surface_get_lod_edge_length(surface, lod));
		}
	}
	p_scenario.member_count = entries.size();
	p_scenario.id_count = id_count;
	p_scenario.bucket_data.resize(p_scenario.buckets.size());
	if (p_scenario.lod_edges.is_empty()) {
		p_scenario.lod_edges.push_back(0.0f); // A storage buffer cannot be empty.
	}

	RD *rd = RD::get_singleton();
	struct Grow {
		RID *buffer;
		uint32_t *capacity;
		uint32_t needed;
		uint32_t minimum;
		uint32_t element_size;
		bool indirect;
	};
	const Grow grows[] = {
		{ &p_scenario.members_buffer, &p_scenario.member_capacity, p_scenario.member_count, 64, 2 * sizeof(uint32_t), false },
		{ &p_scenario.buckets_buffer, &p_scenario.bucket_capacity, p_scenario.buckets.size(), 16, sizeof(GPUInstanceBucketData), false },
		{ &p_scenario.commands_buffer, &p_scenario.command_capacity, p_scenario.commands.size() / 5, 16, 5 * sizeof(uint32_t), true },
		{ &p_scenario.lod_edges_buffer, &p_scenario.lod_edge_capacity, p_scenario.lod_edges.size(), 16, sizeof(float), false },
		{ &p_scenario.ids_buffer, &p_scenario.id_capacity, id_count, 64, sizeof(uint32_t), false },
	};
	for (const Grow &grow : grows) {
		if (*grow.capacity >= grow.needed && grow.buffer->is_valid()) {
			continue;
		}
		if (grow.buffer->is_valid()) {
			rd->free_rid(*grow.buffer);
		}
		*grow.capacity = Math::next_power_of_2(MAX(grow.needed, grow.minimum));
		BitField<RD::StorageBufferUsage> usage = {};
		if (grow.indirect) {
			usage.set_flag(RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		}
		*grow.buffer = rd->storage_buffer_create(*grow.capacity * grow.element_size, {}, usage);
		if (grow.buffer == &p_scenario.members_buffer) {
			// The instance data has one record per member.
			if (p_scenario.output_buffer.is_valid()) {
				rd->free_rid(p_scenario.output_buffer);
			}
			p_scenario.output_buffer = rd->storage_buffer_create(*grow.capacity * sizeof(SceneState::InstanceData));
		}
	}
	if (p_scenario.member_count > 0) {
		rd->buffer_update(p_scenario.members_buffer, 0, p_scenario.members.size() * sizeof(uint32_t), p_scenario.members.ptr());
	}
	rd->buffer_update(p_scenario.lod_edges_buffer, 0, p_scenario.lod_edges.size() * sizeof(float), p_scenario.lod_edges.ptr());
	p_scenario.dirty = false;
}

bool RenderForwardClustered::_gpu_instances_begin_view(RenderDataRD *p_render_data, bool p_using_motion_pass, bool p_using_sdfgi, bool p_using_opaque_gi) {
	static_assert(sizeof(GPUInstanceBucketData) == 80);
	static_assert(sizeof(SceneState::InstanceData) == 176 || sizeof(real_t) != sizeof(float));
	static_assert(INSTANCE_DATA_FLAGS_DYNAMIC == (1 << 3) && INSTANCE_DATA_FLAG_USE_GI_BUFFERS == (1 << 5) && INSTANCE_DATA_FLAG_USE_SDFGI == (1 << 6) && INSTANCE_DATA_FLAGS_FADE_SHIFT == 24,
			"Update the instance flags in gpu_instance_cull.glsl.");
	gpu_instances_view = nullptr;
	gpu_instances_left_view.clear();
	p_render_data->gpu_instances_drawn = false;
	if (!gpu_instances_active || !p_render_data->gpu_instances || p_render_data->gpu_scenario == 0) {
		return false;
	}
	// Views the first version leaves to the CPU list: probes, mirrors, XR,
	// temporal upscaling (motion list) and debug draw modes.
	const RenderSceneDataRD *scene = p_render_data->scene_data;
	if (p_render_data->reflection_probe.is_valid() || scene->view_count != 1 || scene->mirror_pass || p_using_motion_pass ||
			get_debug_draw_mode() != RSE::VIEWPORT_DEBUG_DRAW_DISABLED) {
		return false;
	}
	RD *rd = RD::get_singleton();
	if (gpu_instance_table.get_buffer().is_null()) {
		return false;
	}
	if (gpu_instance_cull.version.is_null()) {
		Vector<String> modes;
		modes.push_back("");
		gpu_instance_cull.shader.initialize(modes);
		gpu_instance_cull.version = gpu_instance_cull.shader.version_create();
		gpu_instance_cull.pipeline = rd->compute_pipeline_create(gpu_instance_cull.shader.version_get_shader(gpu_instance_cull.version, 0));
	}
	if (gpu_instance_cull.pipeline.is_null()) {
		return false;
	}

	_update_dirty_geometry_instances(); // Leaves and joins of this render first.
	p_render_data->gpu_instances_drawn = true;
	for (GeometryInstanceForwardClustered *instance : gpu_instances_left) {
		if (instance->gpu_scenario == p_render_data->gpu_scenario && !instance->gpu_drawn && (instance->layer_mask & p_render_data->gpu_visible_layers)) {
			gpu_instances_left_view.push_back(instance);
		}
	}
	GPUInstanceScenario **found = gpu_instance_scenarios.getptr(p_render_data->gpu_scenario);
	if (!found) {
		return true; // Nothing drawn by the GPU path in this scenario.
	}
	GPUInstanceScenario *scenario = *found;
	if (scenario->dirty) {
		_gpu_instances_rebuild(*scenario);
	}
	const uint64_t max_members = uint64_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)) * 64;
	if (scenario->member_count == 0 || scenario->member_count > max_members || scenario->members_buffer.is_null() || scenario->output_buffer.is_null() ||
			scenario->buckets_buffer.is_null() || scenario->commands_buffer.is_null() || scenario->ids_buffer.is_null() || scenario->lod_edges_buffer.is_null()) {
		ERR_FAIL_COND_V_MSG(scenario->member_count > max_members, true, "Too many instance surfaces for the GPU instance path; they are not drawn.");
		return true;
	}

	// Per-surface values, as _fill_instance_data() writes them (the material
	// feedback index can change while streaming runs).
	uint32_t lod_edges_first = 0;
	for (uint32_t i = 0; i < scenario->buckets.size(); i++) {
		const GeometryInstanceSurfaceDataCache *surface = scenario->buckets[i].surface;
		GPUInstanceBucketData &data = scenario->bucket_data[i];
		AABB surface_aabb = AABB(Vector3(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0));
		Vector4 uv_scale = Vector4(0.0, 0.0, 0.0, 0.0);
		if (RendererRD::MeshStorage::get_singleton()->mesh_surface_get_format(surface->surface) & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
			surface_aabb = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_aabb(surface->surface);
			uv_scale = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_uv_scale(surface->surface);
		}
		uint32_t material_feedback_index = UINT32_MAX;
#ifdef MODULE_TEXTURE_STREAMING_ENABLED
		if (surface->material->material_feedback_rid.is_valid()) {
			material_feedback_index = TextureStreaming::get_singleton()->feedback_buffer_material_index(surface->material->material_feedback_rid);
		}
#endif
		for (uint32_t axis = 0; axis < 3; axis++) {
			data.compressed_aabb_position[axis] = surface_aabb.position[axis];
			data.compressed_aabb_size[axis] = surface_aabb.size[axis];
		}
		for (uint32_t component = 0; component < 4; component++) {
			data.uv_scale[component] = uv_scale[component];
		}
		data.ids_first = scenario->buckets[i].ids_first;
		data.material_feedback_index = material_feedback_index;
		data.command = scenario->buckets[i].command;
		data.count = scenario->buckets[i].count;
		data.lod_count = scenario->buckets[i].levels - 1;
		data.lod_edges_first = lod_edges_first;
		data.depth_command = scenario->buckets[i].depth_command;
		lod_edges_first += data.lod_count;
	}
	rd->buffer_update(scenario->buckets_buffer, 0, scenario->bucket_data.size() * sizeof(GPUInstanceBucketData), scenario->bucket_data.ptr());
	// Zero instances in every bucket's command, then cull and append.
	rd->buffer_update(scenario->commands_buffer, 0, scenario->commands.size() * sizeof(uint32_t), scenario->commands.ptr());

	GPUInstanceCullParams params = {};
	const Vector<Plane> planes = _jitter_safe_projection(scene->cam_projection, scene->taa_jitter).get_projection_planes(scene->cam_transform);
	ERR_FAIL_COND_V(planes.size() != 6, true);
	for (uint32_t i = 0; i < 6; i++) {
		params.planes[i][0] = planes[i].normal.x;
		params.planes[i][1] = planes[i].normal.y;
		params.planes[i][2] = planes[i].normal.z;
		params.planes[i][3] = planes[i].d;
	}
	const int validate = GLOBAL_GET_CACHED(int, "rendering/geometry/gpu_instances/validate");
	// Self-test: the last bucket's members are left out, so it draws nothing
	// and both the count check and an image comparison must notice.
	params.member_count = validate == 2 ? scenario->buckets[scenario->buckets.size() - 1].first : scenario->member_count;
	params.visible_layers = p_render_data->gpu_visible_layers;
	params.view_flags = (p_using_opaque_gi ? GPU_INSTANCE_VIEW_USE_GI_BUFFERS : 0) | (p_using_sdfgi ? GPU_INSTANCE_VIEW_USE_SDFGI : 0) | (scene->cam_orthogonal ? GPU_INSTANCE_VIEW_ORTHOGONAL : 0);
	// The LOD inputs of _fill_render_list().
	params.lod_threshold = scene->screen_mesh_lod_threshold;
	const Vector3 lod_camera = scene->main_cam_transform.origin;
	params.lod_camera[0] = lod_camera.x;
	params.lod_camera[1] = lod_camera.y;
	params.lod_camera[2] = lod_camera.z;
	params.lod_distance_multiplier = scene->lod_distance_multiplier;

	const RID shader = gpu_instance_cull.shader.version_get_shader(gpu_instance_cull.version, 0);
	const RID resources[7] = { gpu_instance_table.get_buffer(), scenario->members_buffer, scenario->buckets_buffer, scenario->commands_buffer, scenario->output_buffer, scenario->ids_buffer, scenario->lod_edges_buffer };
	LocalVector<RD::Uniform> uniforms;
	for (uint32_t binding = 0; binding < 7; binding++) {
		RD::Uniform uniform;
		uniform.binding = binding;
		uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		uniform.append_id(resources[binding]);
		uniforms.push_back(uniform);
	}
	RENDER_TIMESTAMP("GPU Instances Cull");
	RD::ComputeListID compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, gpu_instance_cull.pipeline);
	rd->compute_list_bind_uniform_set(compute_list, UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms), 0);
	rd->compute_list_set_push_constant(compute_list, &params, sizeof(params));
	if (params.member_count > 0) {
		rd->compute_list_dispatch(compute_list, (params.member_count + 63) / 64, 1, 1);
	}
	rd->compute_list_end();

	if (validate > 0) {
		const uint64_t frame = RSG::rasterizer->get_frame_number();
		if (!gpu_instances_check_pending && frame % 30 == 0 && frame != gpu_instances_check_frame) {
			// What the CPU cull found visible: the surfaces of the listed instances.
			// Per index LOD as _fill_render_list() would choose it.
			RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
			uint32_t expected = 0;
			for (uint32_t &count : gpu_instances_check_levels) {
				count = 0;
			}
			for (uint32_t i = 0; i < p_render_data->gpu_instances->size(); i++) {
				const GeometryInstanceForwardClustered *instance = static_cast<const GeometryInstanceForwardClustered *>((*p_render_data->gpu_instances)[i]);
				if (!instance->gpu_drawn || instance->gpu_member_scenario != p_render_data->gpu_scenario) {
					continue;
				}
				expected += instance->gpu_surface_count;
				float lod_distance = 1.0;
				if (!scene->cam_orthogonal) {
					const Vector3 aabb_min = instance->transformed_aabb.position;
					const Vector3 aabb_max = instance->transformed_aabb.position + instance->transformed_aabb.size;
					lod_distance = Vector3(0.0, 0.0, 0.0).max(aabb_min - lod_camera).max(lod_camera - aabb_max).length();
				}
				for (const GeometryInstanceSurfaceDataCache *surface = instance->surface_caches; surface; surface = surface->next) {
					uint32_t lod = 0;
					if (scene->screen_mesh_lod_threshold > 0.0 && storage->mesh_surface_has_lod(surface->surface)) {
						uint32_t indices = 0;
						lod = storage->mesh_surface_get_lod(surface->surface, instance->lod_model_scale * instance->lod_bias, lod_distance * scene->lod_distance_multiplier, scene->screen_mesh_lod_threshold, indices);
					}
					gpu_instances_check_levels[MIN(lod, 7u)]++;
				}
			}
			gpu_instances_check_command_levels = scenario->command_levels;
			gpu_instances_check_command_depth = scenario->command_depth;
			gpu_instances_check_frame = frame;
			gpu_instances_check_pending = true;
			gpu_instances_check_expected = expected;
			gpu_instances_check_buckets = scenario->buckets.size();
			gpu_instances_check_members = scenario->member_count;
			rd->buffer_get_data_async(scenario->commands_buffer, callable_mp_static(&RenderForwardClustered::_gpu_instances_check_received), 0, scenario->commands.size() * sizeof(uint32_t));
		}
	}

	gpu_instances_view = scenario;
	return true;
}

void RenderForwardClustered::_gpu_instances_check_received(const Vector<uint8_t> &p_data) {
	RenderForwardClustered *self = get_singleton();
	if (!self) {
		return;
	}
	self->gpu_instances_check_pending = false;
	const uint32_t *words = reinterpret_cast<const uint32_t *>(p_data.ptr());
	const uint32_t word_count = p_data.size() / sizeof(uint32_t);
	uint32_t drawn = 0;
	uint32_t levels[8] = {};
	// The depth pre-pass commands of buckets with a shadow mesh must draw as
	// many instances as their twins.
	uint32_t depth_drawn = 0;
	uint32_t depth_mismatches = 0;
	const LocalVector<uint32_t> &command_depth = self->gpu_instances_check_command_depth;
	for (uint32_t i = 1; i < word_count; i += 5) {
		const uint32_t command = i / 5;
		const uint32_t depth = command < command_depth.size() ? command_depth[command] : UINT32_MAX;
		if (depth == UINT32_MAX - 1) {
			depth_drawn += words[i];
			continue;
		}
		if (depth != UINT32_MAX && (depth * 5 + 1 >= word_count || words[depth * 5 + 1] != words[i])) {
			depth_mismatches++;
		}
		drawn += words[i];
		levels[command < self->gpu_instances_check_command_levels.size() ? self->gpu_instances_check_command_levels[command] : 0] += words[i];
	}
	String drawn_lods;
	String expected_lods;
	bool same_lods = true;
	for (uint32_t level = 0; level < 8; level++) {
		const uint32_t expected = self->gpu_instances_check_levels[level];
		same_lods = same_lods && levels[level] == expected;
		if (levels[level] > 0 || expected > 0) {
			drawn_lods += vformat("%s%d:%d", drawn_lods.is_empty() ? "" : ",", level, levels[level]);
			expected_lods += vformat("%s%d:%d", expected_lods.is_empty() ? "" : ",", level, expected);
		}
	}
	if (depth_mismatches > 0) {
		ERR_PRINT(vformat("GPU instances check (frame %d): %d depth pre-pass commands of shadow meshes draw other instance counts than their buckets.", self->gpu_instances_check_frame, depth_mismatches));
	} else if (drawn != self->gpu_instances_check_expected) {
		ERR_PRINT(vformat("GPU instances check (frame %d): the GPU path drew %d instance surfaces, the CPU cull found %d visible.", self->gpu_instances_check_frame, drawn, self->gpu_instances_check_expected));
	} else if (!same_lods) {
		ERR_PRINT(vformat("GPU instances check (frame %d): instance surfaces per index LOD %s, the CPU choice gives %s.", self->gpu_instances_check_frame, drawn_lods, expected_lods));
	} else {
		print_line(vformat("GPU_INSTANCES_CHECK: ok frame=%d buckets=%d members=%d drawn=%d lods=%s shadow_meshes=%d", self->gpu_instances_check_frame, self->gpu_instances_check_buckets, self->gpu_instances_check_members, drawn, drawn_lods, depth_drawn));
	}
}

void RenderForwardClustered::_gpu_instances_append(int *p_render_info, float p_lod_threshold) {
	if (!gpu_instances_view) {
		return;
	}
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	uint32_t draws = 0;
	for (uint32_t i = 0; i < gpu_instances_view->buckets.size(); i++) {
		const GPUInstanceBucket &bucket = gpu_instances_view->buckets[i];
		GeometryInstanceSurfaceDataCache *surface = bucket.surface;
		// The CPU list did not visit the surface in this view.
		surface->color_pass_inclusion_mask = 0;
		// One element per index LOD; a view without LODs fills level 0 only.
		const uint32_t levels = p_lod_threshold > 0.0f ? bucket.levels : 1;
		for (uint32_t level = 0; level < levels; level++) {
			list.elements.push_back(surface);
			RenderElementInfo info;
			info.value = uint32_t(surface->sort.sort_key1 & 0x300); // Soft shadow and projector bits; no lightmap or forward GI.
			info.lod_index = level;
			info.repeat = 1;
			info.multimesh_cull_draw = UINT32_MAX;
			info.multimesh_cull_lod = 0;
			info.multimesh_cull_last_lod = 0;
			info.mesh_clod_offset = UINT32_MAX;
			info.gpu_bucket = bucket.command + level;
			info.gpu_depth_bucket = (bucket.depth_command == UINT32_MAX ? bucket.command : bucket.depth_command) + level;
			info.gpu_first = bucket.ids_first + level * bucket.count;
			list.element_info.push_back(info);
		}
		draws += levels;
	}
	if (p_render_info) {
		p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += draws;
	}
}
