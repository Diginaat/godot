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
	uint32_t member_count;
	uint32_t view_flags;
	float lod_threshold;
	float lod_distance_multiplier;
	float lod_camera[3];
	uint32_t pad;
};
static_assert(sizeof(GPUInstanceCullParams) == 32);

enum {
	GPU_INSTANCE_VIEW_USE_GI_BUFFERS = 1,
	GPU_INSTANCE_VIEW_USE_SDFGI = 2,
	GPU_INSTANCE_VIEW_ORTHOGONAL = 4,
	GPU_INSTANCE_VIEW_MOTION = 8, // Members that moved go to the motion pass's commands.
};

// As in render_forward_clustered.cpp.
uint32_t gpu_instance_indices_to_primitives(RSE::PrimitiveType p_primitive, uint32_t p_indices) {
	static const uint32_t divisor[RSE::PRIMITIVE_MAX] = { 1, 2, 1, 3, 1 };
	static const uint32_t subtractor[RSE::PRIMITIVE_MAX] = { 0, 0, 1, 0, 2 };
	return (p_indices - subtractor[p_primitive]) / divisor[p_primitive];
}

// Marks the commands of moved members in GPUInstanceScenario::command_levels.
constexpr uint8_t GPU_INSTANCE_MOVED_COMMAND = 0x80;

void gpu_instances_free_buffers(RID *const (&p_buffers)[9]) {
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
			_gpu_instances_join(**scenario, instance); // Placed in buckets before the next view draws.
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
		return; // Its surfaces are being rebuilt; the update refreshes it.
	}
	uint32_t surface_count = 0;
	for (const GeometryInstanceSurfaceDataCache *surface = p_instance->surface_caches; surface; surface = surface->next) {
		surface_count++;
	}
	p_instance->gpu_surface_count = surface_count;

	const uint64_t scenario = (gpu_instances_active && p_instance->gpu_scenario != 0 && _gpu_instance_eligible(p_instance)) ? p_instance->gpu_scenario : 0;
	if (scenario == p_instance->gpu_member_scenario) {
		if (scenario != 0 && p_instance->gpu_member_head != UINT32_MAX) {
			// Its surfaces may have changed (and the old ones may be freed): place
			// them again.
			GPUInstanceScenario *target = gpu_instance_scenarios[scenario];
			_gpu_instances_unplace(*target, p_instance);
			_gpu_instances_join(*target, p_instance);
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
	_gpu_instances_unjoin(*scenario, p_instance);
	_gpu_instances_unplace(*scenario, p_instance);

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
		gpu_instances_free_buffers({ &scenario->members_buffer, &scenario->buckets_buffer, &scenario->commands_buffer, &scenario->lod_edges_buffer, &scenario->output_buffer, &scenario->ids_buffer, &scenario->command_primitives_buffer, &scenario->stats_buffer, &scenario->visible_buffer });
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
			instance->gpu_member_head = UINT32_MAX;
			instance->gpu_join_index = UINT32_MAX;
			instance->gpu_drawn = false;
		}
		gpu_instances_free_buffers({ &scenario->members_buffer, &scenario->buckets_buffer, &scenario->commands_buffer, &scenario->lod_edges_buffer, &scenario->output_buffer, &scenario->ids_buffer, &scenario->command_primitives_buffer, &scenario->stats_buffer, &scenario->visible_buffer });
		memdelete(scenario);
	}
	gpu_instance_scenarios.clear();
	gpu_instances_pending.clear();
	gpu_instances_left.clear();
	gpu_instances_left_view.clear();
	gpu_instances_view_info.clear();
}

void RenderForwardClustered::_gpu_instances_join(GPUInstanceScenario &p_scenario, GeometryInstanceForwardClustered *p_instance) {
	if (p_instance->gpu_join_index != UINT32_MAX || !p_instance->gpu_drawn) {
		return; // Members not drawn yet are placed by the rebuild or at their first cull.
	}
	p_instance->gpu_join_index = p_scenario.joins.size();
	p_scenario.joins.push_back(p_instance);
}

void RenderForwardClustered::_gpu_instances_unjoin(GPUInstanceScenario &p_scenario, GeometryInstanceForwardClustered *p_instance) {
	const uint32_t index = p_instance->gpu_join_index;
	if (index == UINT32_MAX) {
		return;
	}
	ERR_FAIL_COND(index >= p_scenario.joins.size() || p_scenario.joins[index] != p_instance);
	GeometryInstanceForwardClustered *last = p_scenario.joins[p_scenario.joins.size() - 1];
	p_scenario.joins[index] = last;
	last->gpu_join_index = index;
	p_scenario.joins.resize(p_scenario.joins.size() - 1);
	p_instance->gpu_join_index = UINT32_MAX;
}

void RenderForwardClustered::_gpu_instances_unplace(GPUInstanceScenario &p_scenario, GeometryInstanceForwardClustered *p_instance) {
	// Its surfaces may already be freed: only the member slots are touched.
	uint32_t member = p_instance->gpu_member_head;
	p_instance->gpu_member_head = UINT32_MAX;
	while (member != UINT32_MAX) {
		const uint32_t next = p_scenario.member_next[member];
		GPUInstanceBucket &bucket = p_scenario.buckets[p_scenario.members[member * 2 + 1]];
		p_scenario.member_owner[member] = nullptr;
		p_scenario.member_surface[member] = nullptr;
		p_scenario.members[member * 2 + 0] = UINT32_MAX;
		p_scenario.member_next[member] = bucket.free_head;
		bucket.free_head = member;
		bucket.live--;
		if (bucket.rep == member) {
			bucket.rep = UINT32_MAX;
			bucket.surface = nullptr;
			p_scenario.reps_dirty = true;
		}
		p_scenario.live_members--;
		p_scenario.free_members++;
		p_scenario.changed_members.push_back(member);
		member = next;
	}
}

bool RenderForwardClustered::_gpu_instances_place(GPUInstanceScenario &p_scenario, GeometryInstanceForwardClustered *p_instance) {
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	// Every surface needs a bucket with the same key and mesh, and room.
	for (GeometryInstanceSurfaceDataCache *surface = p_instance->surface_caches; surface; surface = surface->next) {
		const uint32_t *found = p_scenario.bucket_map.getptr({ surface->sort.sort_key1 & ~GPU_INSTANCE_KEY1_VIEW_BITS, surface->sort.sort_key2, uint32_t(p_instance->mirror) });
		if (!found) {
			return false;
		}
		const GPUInstanceBucket &bucket = p_scenario.buckets[*found];
		const uint32_t lod_count = storage->mesh_surface_has_lod(surface->surface) ? storage->mesh_surface_get_lod_count(surface->surface) : 0;
		if (bucket.mesh_surface != surface->surface || bucket.shadow_surface != surface->surface_shadow || bucket.levels != lod_count + 1 ||
				p_scenario.commands[bucket.command * 5] != storage->mesh_surface_get_vertices_drawn_count(surface->surface)) {
			return false;
		}
		if (bucket.free_head == UINT32_MAX && bucket.count >= bucket.capacity) {
			return false;
		}
	}
	for (GeometryInstanceSurfaceDataCache *surface = p_instance->surface_caches; surface; surface = surface->next) {
		const uint32_t index = p_scenario.bucket_map[{ surface->sort.sort_key1 & ~GPU_INSTANCE_KEY1_VIEW_BITS, surface->sort.sort_key2, uint32_t(p_instance->mirror) }];
		GPUInstanceBucket &bucket = p_scenario.buckets[index];
		uint32_t member;
		if (bucket.free_head != UINT32_MAX) {
			member = bucket.free_head;
			bucket.free_head = p_scenario.member_next[member];
			p_scenario.free_members--;
		} else {
			member = bucket.first + bucket.count++;
		}
		p_scenario.member_owner[member] = p_instance;
		p_scenario.member_surface[member] = surface;
		p_scenario.members[member * 2 + 0] = p_instance->gpu_slot;
		p_scenario.members[member * 2 + 1] = index;
		p_scenario.member_next[member] = p_instance->gpu_member_head;
		p_instance->gpu_member_head = member;
		bucket.live++;
		if (bucket.rep == UINT32_MAX) {
			bucket.rep = member;
			bucket.surface = surface;
		}
		p_scenario.live_members++;
		p_scenario.changed_members.push_back(member);
	}
	return true;
}

void RenderForwardClustered::_gpu_instances_update_members(GPUInstanceScenario &p_scenario) {
	if (p_scenario.changed_members.is_empty()) {
		return;
	}
	// Contiguous runs; many runs go up as the whole member list.
	LocalVector<uint32_t> &changed = p_scenario.changed_members;
	changed.sort();
	uint32_t runs = 0;
	for (uint32_t i = 0; i < changed.size(); i++) {
		if (i == 0 || changed[i] > changed[i - 1] + 1) {
			runs++;
		}
	}
	RD *rd = RD::get_singleton();
	if (runs > 16) {
		rd->buffer_update(p_scenario.members_buffer, 0, p_scenario.members.size() * sizeof(uint32_t), p_scenario.members.ptr());
	} else {
		uint32_t start = 0;
		for (uint32_t i = 1; i <= changed.size(); i++) {
			if (i == changed.size() || changed[i] > changed[i - 1] + 1) {
				const uint32_t from = changed[start];
				const uint32_t to = changed[i - 1];
				rd->buffer_update(p_scenario.members_buffer, from * 2 * sizeof(uint32_t), (to - from + 1) * 2 * sizeof(uint32_t), p_scenario.members.ptr() + from * 2);
				start = i;
			}
		}
	}
	changed.clear();
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
	gpu_instances_rebuilds++;
	for (GeometryInstanceForwardClustered *joining : p_scenario.joins) {
		joining->gpu_join_index = UINT32_MAX;
	}
	p_scenario.joins.clear();
	for (GeometryInstanceForwardClustered *instance : p_scenario.instances) {
		instance->gpu_member_head = UINT32_MAX;
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
	p_scenario.command_primitives.clear();
	p_scenario.lod_edges.clear();
	p_scenario.bucket_map.clear();
	for (uint32_t i = 0; i < entries.size(); i++) {
		if (i == 0 || !entries[i].same_draw(entries[i - 1])) {
			GPUInstanceBucket bucket;
			bucket.surface = entries[i].surface;
			bucket.mesh_surface = entries[i].surface->surface;
			bucket.shadow_surface = entries[i].surface->surface_shadow;
			p_scenario.bucket_map.insert({ entries[i].key1, entries[i].key2, entries[i].mirror }, p_scenario.buckets.size());
			p_scenario.buckets.push_back(bucket);
		}
		p_scenario.buckets[p_scenario.buckets.size() - 1].count++;
	}
	// Spare slots per bucket, so that surfaces can join without a rebuild.
	uint32_t slots = 0;
	for (GPUInstanceBucket &bucket : p_scenario.buckets) {
		bucket.first = slots;
		bucket.capacity = bucket.count + MAX(bucket.count / 4, 32u);
		bucket.live = bucket.count;
		slots += bucket.capacity;
	}
	p_scenario.members.resize(slots * 2);
	p_scenario.member_owner.resize(slots);
	p_scenario.member_surface.resize(slots);
	p_scenario.member_next.resize(slots);
	for (uint32_t b = 0; b < p_scenario.buckets.size(); b++) {
		const GPUInstanceBucket &bucket = p_scenario.buckets[b];
		for (uint32_t member = bucket.first; member < bucket.first + bucket.capacity; member++) {
			p_scenario.members[member * 2 + 0] = UINT32_MAX;
			p_scenario.members[member * 2 + 1] = b;
			p_scenario.member_owner[member] = nullptr;
			p_scenario.member_surface[member] = nullptr;
			p_scenario.member_next[member] = UINT32_MAX;
		}
	}
	uint32_t bucket_index = 0;
	uint32_t in_bucket = 0;
	for (uint32_t i = 0; i < entries.size(); i++) {
		if (i > 0 && !entries[i].same_draw(entries[i - 1])) {
			bucket_index++;
			in_bucket = 0;
		}
		GPUInstanceBucket &bucket = p_scenario.buckets[bucket_index];
		const uint32_t member = bucket.first + in_bucket++;
		GeometryInstanceForwardClustered *owner = static_cast<GeometryInstanceForwardClustered *>(entries[i].surface->owner);
		p_scenario.members[member * 2 + 0] = entries[i].slot;
		p_scenario.member_owner[member] = owner;
		p_scenario.member_surface[member] = entries[i].surface;
		p_scenario.member_next[member] = owner->gpu_member_head;
		owner->gpu_member_head = member;
		if (bucket.rep == UINT32_MAX) {
			bucket.rep = member;
		}
	}
	p_scenario.live_members = entries.size();
	p_scenario.free_members = 0;
	p_scenario.reps_dirty = false;
	p_scenario.changed_members.clear();

	// Per bucket, one indexed or non-indexed command per index LOD (the kernel
	// writes word 1), for members that did not move and again for those that
	// did, and count visible IDs reserved per command.
	uint32_t id_count = 0;
	for (GPUInstanceBucket &bucket : p_scenario.buckets) {
		void *surface = bucket.surface->surface;
		const uint32_t lod_count = storage->mesh_surface_has_lod(surface) ? storage->mesh_surface_get_lod_count(surface) : 0;
		bucket.levels = lod_count + 1;
			bucket.command = p_scenario.commands.size() / 5;
		bucket.ids_first = id_count;
		id_count += bucket.capacity * bucket.levels * 2;
		for (uint32_t command = 0; command < bucket.levels * 2; command++) {
			const uint32_t level = command % bucket.levels;
			p_scenario.commands.push_back(level == 0 ? storage->mesh_surface_get_vertices_drawn_count(surface) : storage->mesh_surface_get_index_count(surface, level));
			for (uint32_t word = 1; word < 5; word++) {
				p_scenario.commands.push_back(0);
			}
			p_scenario.command_levels.push_back(uint8_t(MIN(level, 7u)) | (command >= bucket.levels ? GPU_INSTANCE_MOVED_COMMAND : 0));
			p_scenario.command_depth.push_back(UINT32_MAX);
			p_scenario.command_primitives.push_back(gpu_instance_indices_to_primitives(bucket.surface->primitive, p_scenario.commands[p_scenario.commands.size() - 5]));
		}
		bucket.depth_command = UINT32_MAX;
		void *shadow = bucket.surface->surface_shadow;
		if (shadow != surface) {
			// The same instance counts, with the shadow mesh's index counts.
			bucket.depth_command = p_scenario.commands.size() / 5;
			for (uint32_t command = 0; command < bucket.levels * 2; command++) {
				const uint32_t level = command % bucket.levels;
				p_scenario.commands.push_back(level == 0 ? storage->mesh_surface_get_vertices_drawn_count(shadow) : storage->mesh_surface_get_index_count(shadow, level));
				for (uint32_t word = 1; word < 5; word++) {
					p_scenario.commands.push_back(0);
				}
				p_scenario.command_levels.push_back(0);
				p_scenario.command_depth.push_back(UINT32_MAX - 1);
				p_scenario.command_primitives.push_back(0);
				p_scenario.command_depth[bucket.command + command] = bucket.depth_command + command;
			}
		}
		for (uint32_t lod = 0; lod < lod_count; lod++) {
			p_scenario.lod_edges.push_back(storage->mesh_surface_get_lod_edge_length(surface, lod));
		}
	}
	p_scenario.member_count = slots;
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
		{ &p_scenario.command_primitives_buffer, &p_scenario.command_primitive_capacity, p_scenario.commands.size() / 5, 16, sizeof(uint32_t), false },
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
	rd->buffer_update(p_scenario.command_primitives_buffer, 0, p_scenario.command_primitives.size() * sizeof(uint32_t), p_scenario.command_primitives.ptr());
	if (p_scenario.stats_buffer.is_null()) {
		p_scenario.stats_buffer = rd->storage_buffer_create(4 * sizeof(uint32_t));
	}
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
	// Views the first version leaves to the CPU list: probes, mirrors, XR and
	// debug draw modes.
	const RenderSceneDataRD *scene = p_render_data->scene_data;
	if (p_render_data->reflection_probe.is_valid() || scene->view_count != 1 || scene->mirror_pass ||
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
	if (!scenario->dirty) {
		// Surfaces that joined go to free slots of their buckets; a new key or
		// a full bucket makes it a rebuild, and so do many free slots.
		for (uint32_t i = 0; i < scenario->joins.size() && !scenario->dirty; i++) {
			GeometryInstanceForwardClustered *instance = scenario->joins[i];
			if (instance->gpu_drawn && instance->gpu_member_head == UINT32_MAX && !_gpu_instances_place(*scenario, instance)) {
				scenario->dirty = true;
			}
		}
		if (!scenario->dirty) {
			for (GeometryInstanceForwardClustered *instance : scenario->joins) {
				instance->gpu_join_index = UINT32_MAX;
			}
			scenario->joins.clear();
		}
		if (scenario->free_members > 64 && scenario->free_members > scenario->live_members) {
			scenario->dirty = true;
		}
	}
	if (scenario->dirty) {
		_gpu_instances_rebuild(*scenario);
	} else {
		if (scenario->reps_dirty) {
			for (GPUInstanceBucket &bucket : scenario->buckets) {
				if (bucket.rep != UINT32_MAX || bucket.live == 0) {
					continue;
				}
				for (uint32_t member = bucket.first; member < bucket.first + bucket.count; member++) {
					if (scenario->member_owner[member]) {
						bucket.rep = member;
						bucket.surface = scenario->member_surface[member];
						break;
					}
				}
			}
			scenario->reps_dirty = false;
		}
		_gpu_instances_update_members(*scenario);
	}
	const uint64_t max_members = uint64_t(rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)) * 64;
	if (scenario->member_count == 0 || scenario->member_count > max_members || scenario->members_buffer.is_null() || scenario->output_buffer.is_null() ||
			scenario->buckets_buffer.is_null() || scenario->commands_buffer.is_null() || scenario->ids_buffer.is_null() || scenario->lod_edges_buffer.is_null() ||
			scenario->command_primitives_buffer.is_null() || scenario->stats_buffer.is_null()) {
		ERR_FAIL_COND_V_MSG(scenario->member_count > max_members, true, "Too many instance surfaces for the GPU instance path; they are not drawn.");
		return true;
	}

	// Per-surface values, as _fill_instance_data() writes them (the material
	// feedback index can change while streaming runs).
	uint32_t lod_edges_first = 0;
	for (uint32_t i = 0; i < scenario->buckets.size(); i++) {
		const GeometryInstanceSurfaceDataCache *surface = scenario->buckets[i].surface; // Null: no live members.
		GPUInstanceBucketData &data = scenario->bucket_data[i];
		AABB surface_aabb = AABB(Vector3(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0));
		Vector4 uv_scale = Vector4(0.0, 0.0, 0.0, 0.0);
		if (surface && (RendererRD::MeshStorage::get_singleton()->mesh_surface_get_format(surface->surface) & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
			surface_aabb = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_aabb(surface->surface);
			uv_scale = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_uv_scale(surface->surface);
		}
		uint32_t material_feedback_index = UINT32_MAX;
#ifdef MODULE_TEXTURE_STREAMING_ENABLED
		if (surface && surface->material->material_feedback_rid.is_valid()) {
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
		data.capacity = scenario->buckets[i].capacity;
		data.lod_count = scenario->buckets[i].levels - 1;
		data.lod_edges_first = lod_edges_first;
		data.depth_command = scenario->buckets[i].depth_command;
		lod_edges_first += data.lod_count;
	}
	rd->buffer_update(scenario->buckets_buffer, 0, scenario->bucket_data.size() * sizeof(GPUInstanceBucketData), scenario->bucket_data.ptr());
	// Zero instances in every bucket's command and the view's counts, then cull and append.
	rd->buffer_update(scenario->commands_buffer, 0, scenario->commands.size() * sizeof(uint32_t), scenario->commands.ptr());
	const uint32_t zero_stats[4] = {};
	rd->buffer_update(scenario->stats_buffer, 0, sizeof(zero_stats), zero_stats);

	// What the scene cull found visible, a bit per table slot: the cull pass
	// draws exactly those (frustum, layers and occluders as the CPU tested).
	const uint32_t words = MAX((gpu_instance_slots.get_capacity() + 31) / 32, 1u);
	LocalVector<uint32_t> &bits = gpu_instances_visible_bits;
	bits.resize(words);
	memset(bits.ptr(), 0, words * sizeof(uint32_t));
	// The cull listed the slots with the instances (no pointer chasing here).
	// Instances that left during this render have no members: their bits
	// draw nothing.
	if (p_render_data->gpu_slots) {
		for (uint32_t slot : *p_render_data->gpu_slots) {
			if (slot < words * 32) {
				bits[slot >> 5] |= 1u << (slot & 31);
			}
		}
	}
	if (scenario->visible_capacity < words || scenario->visible_buffer.is_null()) {
		if (scenario->visible_buffer.is_valid()) {
			rd->free_rid(scenario->visible_buffer);
		}
		scenario->visible_capacity = Math::next_power_of_2(MAX(words, 64u));
		scenario->visible_buffer = rd->storage_buffer_create(scenario->visible_capacity * sizeof(uint32_t));
	}
	rd->buffer_update(scenario->visible_buffer, 0, words * sizeof(uint32_t), bits.ptr());

	GPUInstanceCullParams params = {};
	const int validate = GLOBAL_GET_CACHED(int, "rendering/geometry/gpu_instances/validate");
	// Self-test: the last bucket's members are left out, so it draws nothing
	// and both the count check and an image comparison must notice.
	params.member_count = validate == 2 ? scenario->buckets[scenario->buckets.size() - 1].first : scenario->member_count;
	// Members can have moved only if an instance moved in this or the last
	// frame; otherwise the motion pass gets no GPU path draws (and the
	// validation reports moved members drawn without it).
	const uint64_t frame = RSG::rasterizer->get_frame_number();
	gpu_instances_view_motion = p_using_motion_pass && gpu_instances_moved_frame + 1 >= frame;
	params.view_flags = (p_using_opaque_gi ? GPU_INSTANCE_VIEW_USE_GI_BUFFERS : 0) | (p_using_sdfgi ? GPU_INSTANCE_VIEW_USE_SDFGI : 0) | (scene->cam_orthogonal ? GPU_INSTANCE_VIEW_ORTHOGONAL : 0) |
			(gpu_instances_view_motion ? GPU_INSTANCE_VIEW_MOTION : 0);
	// The LOD inputs of _fill_render_list().
	params.lod_threshold = scene->screen_mesh_lod_threshold;
	const Vector3 lod_camera = scene->main_cam_transform.origin;
	params.lod_camera[0] = lod_camera.x;
	params.lod_camera[1] = lod_camera.y;
	params.lod_camera[2] = lod_camera.z;
	params.lod_distance_multiplier = scene->lod_distance_multiplier;

	const RID shader = gpu_instance_cull.shader.version_get_shader(gpu_instance_cull.version, 0);
	const RID resources[10] = { gpu_instance_table.get_buffer(), scenario->members_buffer, scenario->buckets_buffer, scenario->commands_buffer, scenario->output_buffer, scenario->ids_buffer, scenario->lod_edges_buffer,
		scenario->command_primitives_buffer, scenario->stats_buffer, scenario->visible_buffer };
	LocalVector<RD::Uniform> uniforms;
	for (uint32_t binding = 0; binding < 10; binding++) {
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

	// Render info: what this view drew, known a few frames later.
	bool check_stats = false;
	gpu_instances_view_key = uint64_t(p_render_data->render_buffers.ptr());
	if (gpu_instances_view_key != 0) {
		GPUInstanceViewInfo *info = gpu_instances_view_info.getptr(gpu_instances_view_key);
		if (!info) {
			info = &gpu_instances_view_info.insert(gpu_instances_view_key, GPUInstanceViewInfo())->value;
		}
		info->frame = frame;
		check_stats = validate > 0 && !gpu_instances_check_pending && frame % 30 == 0 && frame != gpu_instances_check_frame;
		rd->buffer_get_data_async(scenario->stats_buffer, callable_mp_static(&RenderForwardClustered::_gpu_instances_stats_received).bind(gpu_instances_view_key, check_stats), 0, 2 * sizeof(uint32_t));
	}
	// Views that are gone (their render buffers freed) leave the cache.
	if (frame % 600 == 0) {
		LocalVector<uint64_t> stale;
		for (const KeyValue<uint64_t, GPUInstanceViewInfo> &E : gpu_instances_view_info) {
			if (E.value.frame + 600 < frame) {
				stale.push_back(E.key);
			}
		}
		for (uint64_t key : stale) {
			gpu_instances_view_info.erase(key);
		}
	}

	if (validate > 0) {
		if (!gpu_instances_check_pending && frame % 30 == 0 && frame != gpu_instances_check_frame) {
			// What the CPU cull found visible: the surfaces of the listed instances.
			// Per index LOD as _fill_render_list() would choose it.
			RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
			uint32_t expected = 0;
			uint32_t moved = 0;
			uint32_t primitives = 0;
			for (uint32_t &count : gpu_instances_check_levels) {
				count = 0;
			}
			for (uint32_t i = 0; i < p_render_data->gpu_instances->size(); i++) {
				const GeometryInstanceForwardClustered *instance = static_cast<const GeometryInstanceForwardClustered *>((*p_render_data->gpu_instances)[i]);
				if (!instance->gpu_drawn || instance->gpu_member_scenario != p_render_data->gpu_scenario) {
					continue;
				}
				expected += instance->gpu_surface_count;
				if (p_using_motion_pass && instance->transform_status == GeometryInstanceForwardClustered::TransformStatus::MOVED) {
					moved += instance->gpu_surface_count;
				}
				float lod_distance = 1.0;
				if (!scene->cam_orthogonal) {
					const Vector3 aabb_min = instance->transformed_aabb.position;
					const Vector3 aabb_max = instance->transformed_aabb.position + instance->transformed_aabb.size;
					lod_distance = Vector3(0.0, 0.0, 0.0).max(aabb_min - lod_camera).max(lod_camera - aabb_max).length();
				}
				for (const GeometryInstanceSurfaceDataCache *surface = instance->surface_caches; surface; surface = surface->next) {
					uint32_t lod = 0;
					uint32_t indices = storage->mesh_surface_get_vertices_drawn_count(surface->surface);
					if (scene->screen_mesh_lod_threshold > 0.0 && storage->mesh_surface_has_lod(surface->surface)) {
						lod = storage->mesh_surface_get_lod(surface->surface, instance->lod_model_scale * instance->lod_bias, lod_distance * scene->lod_distance_multiplier, scene->screen_mesh_lod_threshold, indices);
					}
					primitives += gpu_instance_indices_to_primitives(surface->primitive, indices);
					gpu_instances_check_levels[MIN(lod, 7u)]++;
				}
			}
			gpu_instances_check_moved = moved;
			// As the CPU lists count them: moved surfaces are in the motion list too.
			gpu_instances_check_objects = expected + (gpu_instances_view_motion ? moved : 0);
			gpu_instances_check_primitives = primitives;
			if (!check_stats) {
				gpu_instances_check_objects = UINT32_MAX; // No stats readback for this check.
			}
			gpu_instances_check_command_levels = scenario->command_levels;
			gpu_instances_check_command_depth = scenario->command_depth;
			gpu_instances_check_frame = frame;
			gpu_instances_check_pending = true;
			gpu_instances_check_expected = expected;
			gpu_instances_check_buckets = scenario->buckets.size();
			gpu_instances_check_members = scenario->live_members;
			rd->buffer_get_data_async(scenario->commands_buffer, callable_mp_static(&RenderForwardClustered::_gpu_instances_check_received), 0, scenario->commands.size() * sizeof(uint32_t));
		}
	}

	gpu_instances_view = scenario;
	return true;
}

void RenderForwardClustered::_gpu_instances_check_received(const Vector<uint8_t> &p_data) {
	RenderForwardClustered *self = get_singleton();
	if (!self || !gpu_instances_readbacks_alive) {
		return;
	}
	self->gpu_instances_check_pending = false;
	const uint32_t *words = reinterpret_cast<const uint32_t *>(p_data.ptr());
	const uint32_t word_count = p_data.size() / sizeof(uint32_t);
	uint32_t drawn = 0;
	uint32_t moved = 0;
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
		const uint8_t level = command < self->gpu_instances_check_command_levels.size() ? self->gpu_instances_check_command_levels[command] : 0;
		levels[level & 7] += words[i];
		if (level & GPU_INSTANCE_MOVED_COMMAND) {
			moved += words[i];
		}
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
	if (moved != self->gpu_instances_check_moved) {
		ERR_PRINT(vformat("GPU instances check (frame %d): the GPU path drew %d moved instance surfaces in the motion pass, the CPU list would draw %d.", self->gpu_instances_check_frame, moved, self->gpu_instances_check_moved));
	} else if (depth_mismatches > 0) {
		ERR_PRINT(vformat("GPU instances check (frame %d): %d depth pre-pass commands of shadow meshes draw other instance counts than their buckets.", self->gpu_instances_check_frame, depth_mismatches));
	} else if (drawn != self->gpu_instances_check_expected) {
		ERR_PRINT(vformat("GPU instances check (frame %d): the GPU path drew %d instance surfaces, the CPU cull found %d visible.", self->gpu_instances_check_frame, drawn, self->gpu_instances_check_expected));
	} else if (!same_lods) {
		ERR_PRINT(vformat("GPU instances check (frame %d): instance surfaces per index LOD %s, the CPU choice gives %s.", self->gpu_instances_check_frame, drawn_lods, expected_lods));
	} else {
		print_line(vformat("GPU_INSTANCES_CHECK: ok frame=%d buckets=%d members=%d drawn=%d lods=%s shadow_meshes=%d moved=%d rebuilds=%d", self->gpu_instances_check_frame, self->gpu_instances_check_buckets, self->gpu_instances_check_members, drawn, drawn_lods, depth_drawn, moved, self->gpu_instances_rebuilds));
	}
}

void RenderForwardClustered::_gpu_instances_stats_received(const Vector<uint8_t> &p_data, uint64_t p_view, bool p_check) {
	RenderForwardClustered *self = get_singleton();
	if (!self || !gpu_instances_readbacks_alive || p_data.size() < int(2 * sizeof(uint32_t))) {
		return;
	}
	const uint32_t *words = reinterpret_cast<const uint32_t *>(p_data.ptr());
	GPUInstanceViewInfo *info = self->gpu_instances_view_info.getptr(p_view);
	if (info) {
		info->objects = words[0];
		info->primitives = words[1];
	}
	if (p_check && self->gpu_instances_check_objects != UINT32_MAX) {
		if (words[0] != self->gpu_instances_check_objects || words[1] != self->gpu_instances_check_primitives) {
			ERR_PRINT(vformat("GPU instances check (frame %d): render info of the GPU path %d objects and %d primitives, the CPU lists would count %d and %d.", self->gpu_instances_check_frame, words[0], words[1], self->gpu_instances_check_objects, self->gpu_instances_check_primitives));
		} else {
			print_line(vformat("GPU_INSTANCES_INFO: ok frame=%d objects=%d primitives=%d", self->gpu_instances_check_frame, words[0], words[1]));
		}
	}
}

void RenderForwardClustered::_gpu_instances_append(int *p_render_info, float p_lod_threshold) {
	if (!gpu_instances_view) {
		return;
	}
	RenderList &list = render_list[RENDER_LIST_OPAQUE];
	RenderList &motion_list = render_list[RENDER_LIST_MOTION];
	uint32_t draws = 0;
	for (uint32_t i = 0; i < gpu_instances_view->buckets.size(); i++) {
		const GPUInstanceBucket &bucket = gpu_instances_view->buckets[i];
		GeometryInstanceSurfaceDataCache *surface = bucket.surface;
		if (!surface) {
			continue; // No live members.
		}
		// One element per index LOD; a view without LODs fills level 0 only.
		// Moved members: the depth pre-pass draws them from the opaque list,
		// the motion pass from the motion list.
		const uint32_t levels = p_lod_threshold > 0.0f ? bucket.levels : 1;
		for (uint32_t moved = 0; moved < (gpu_instances_view_motion ? 2u : 1u); moved++) {
			for (uint32_t level = 0; level < levels; level++) {
				RenderElementInfo info;
				info.value = uint32_t(surface->sort.sort_key1 & 0x300); // Soft shadow and projector bits; no lightmap or forward GI.
				info.lod_index = level;
				info.gpu_motion = moved;
				info.repeat = 1;
				info.multimesh_cull_draw = UINT32_MAX;
				info.multimesh_cull_lod = 0;
				info.multimesh_cull_last_lod = 0;
				info.mesh_clod_offset = UINT32_MAX;
				const uint32_t command = moved * bucket.levels + level;
				info.gpu_bucket = bucket.command + command;
				info.gpu_depth_bucket = (bucket.depth_command == UINT32_MAX ? bucket.command : bucket.depth_command) + command;
				info.gpu_first = bucket.ids_first + command * bucket.capacity;
				list.elements.push_back(surface);
				list.element_info.push_back(info);
				draws++;
				if (moved) {
					motion_list.elements.push_back(surface);
					motion_list.element_info.push_back(info);
					draws++;
				}
			}
		}
	}
	if (p_render_info) {
		p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += draws;
		const GPUInstanceViewInfo *info = gpu_instances_view_info.getptr(gpu_instances_view_key);
		if (info) {
			p_render_info[RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += info->objects;
			p_render_info[RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += info->primitives;
		}
	}
}
