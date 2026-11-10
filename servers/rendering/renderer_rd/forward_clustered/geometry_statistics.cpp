/**************************************************************************/
/*  geometry_statistics.cpp                                               */
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

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"

using namespace RendererSceneRenderImplementation;

// Word layout of the per-viewport statistics buffer, written by the native
// GPU geometry kernels (GEOMETRY_STATISTICS define) with atomic adds.
enum GeometryStatisticsWord {
	STAT_MULTIMESH_INSTANCES_TESTED,
	STAT_MULTIMESH_INSTANCES_VISIBLE,
	STAT_CLOD_GROUPS_TESTED,
	STAT_CLOD_GROUPS_SELECTED,
	STAT_CLOD_GROUPS_VISIBLE,
	STAT_CLOD_TRIANGLES_VISIBLE,
	STAT_HZB_GROUPS_OCCLUDED,
	STAT_HZB_TRIANGLES_OCCLUDED,
	STAT_MULTIMESH_LOD0_VISIBLE, // Followed by LOD levels 1-3.
	// Shadow-pass MultiMesh culling repeats the words above from here
	// (tested, visible, and per-level visible at +8); words +2..+7 are unused.
	STAT_SHADOW_BASE = STAT_MULTIMESH_LOD0_VISIBLE + 4,
	// Two-pass HZB occlusion: selected groups left out of the phase-1 depth
	// pass (occluded when last tested), and those drawn again in phase 2.
	STAT_HZB_GROUPS_DEFERRED = STAT_SHADOW_BASE + 12,
	STAT_HZB_TRIANGLES_DEFERRED,
	STAT_HZB_GROUPS_RECOVERED,
	STAT_HZB_TRIANGLES_RECOVERED,
	// MultiMesh HZB occlusion: frustum-visible instances left out of the
	// colour passes.
	STAT_HZB_INSTANCES_OCCLUDED,
	// GPU path HZB occlusion: drawn instance surfaces left out of the colour
	// passes (gpu_instance_cull.glsl).
	STAT_GPU_INSTANCES_OCCLUDED,
	STAT_WORD_COUNT,
};
static_assert(STAT_SHADOW_BASE == RenderForwardClustered::GEOMETRY_STATISTICS_SHADOW_OFFSET);
static_assert(STAT_WORD_COUNT == RenderForwardClustered::GEOMETRY_STATISTICS_WORDS);

// Polling keeps readback alive for about two seconds at 60 frames per second.
static constexpr uint64_t GEOMETRY_STATISTICS_POLL_FRAMES = 120;

void RenderForwardClustered::_begin_geometry_statistics(RenderBufferDataForwardClustered *p_buffers) {
	auto &statistics = p_buffers->geometry_statistics;
	RD *rd = RD::get_singleton();
	if (statistics.buffer.is_null()) {
		// Always bound: the kernels accumulate unconditionally (a few atomics per
		// frame), so no extra pipeline variants exist for the disabled case.
		statistics.buffer = rd->storage_buffer_create(GEOMETRY_STATISTICS_WORDS * sizeof(uint32_t));
	}
	statistics.culled_surfaces = 0;
	statistics.skipped_surfaces = 0;
	statistics.skipped_instances = 0;
	statistics.clod_surfaces = 0;
	statistics.shadow_culled_surfaces = 0;
	statistics.recording = statistics.buffer.is_valid() && Engine::get_singleton()->get_frames_drawn() < statistics.requested_until_frame;
	if (statistics.recording) {
		rd->buffer_clear(statistics.buffer, 0, GEOMETRY_STATISTICS_WORDS * sizeof(uint32_t));
	}
}

void RenderForwardClustered::_end_geometry_statistics(RenderBufferDataForwardClustered *p_buffers) {
	auto &statistics = p_buffers->geometry_statistics;
	if (!statistics.recording) {
		return;
	}
	statistics.recording = false;
	// Delayed, non-blocking copy; RD calls back once this frame's GPU work is
	// done. The callback resolves the buffers by ObjectID, so a viewport freed in
	// the meantime is skipped instead of being kept alive.
	const Callable callback = callable_mp_static(&RenderForwardClustered::_receive_geometry_statistics).bind(uint64_t(p_buffers->get_instance_id()), statistics.culled_surfaces, statistics.skipped_surfaces, statistics.skipped_instances, statistics.clod_surfaces, statistics.shadow_culled_surfaces, Engine::get_singleton()->get_frames_drawn());
	RD::get_singleton()->buffer_get_data_async(statistics.buffer, callback);
}

void RenderForwardClustered::_receive_geometry_statistics(const Vector<uint8_t> &p_data, uint64_t p_buffers, uint32_t p_culled_surfaces, uint32_t p_skipped_surfaces, uint64_t p_skipped_instances, uint32_t p_clod_surfaces, uint32_t p_shadow_culled_surfaces, uint64_t p_frame) {
	RenderBufferDataForwardClustered *buffers = Object::cast_to<RenderBufferDataForwardClustered>(ObjectDB::get_instance(ObjectID(p_buffers)));
	if (!buffers || p_data.size() < int(GEOMETRY_STATISTICS_WORDS * sizeof(uint32_t))) {
		return;
	}
	const uint32_t *words = reinterpret_cast<const uint32_t *>(p_data.ptr());
	Dictionary result;
	result["frame"] = p_frame;
	result["multimesh_surfaces_culled"] = p_culled_surfaces;
	result["multimesh_surfaces_fully_visible"] = p_skipped_surfaces;
	result["multimesh_instances_fully_visible"] = p_skipped_instances;
	result["multimesh_instances_tested"] = words[STAT_MULTIMESH_INSTANCES_TESTED];
	result["multimesh_instances_visible"] = words[STAT_MULTIMESH_INSTANCES_VISIBLE];
	PackedInt64Array per_level;
	for (uint32_t level = 0; level < 4; level++) {
		per_level.push_back(words[STAT_MULTIMESH_LOD0_VISIBLE + level]);
	}
	result["multimesh_instances_visible_per_lod"] = per_level;
	result["clod_surfaces"] = p_clod_surfaces;
	result["clod_groups_tested"] = words[STAT_CLOD_GROUPS_TESTED];
	result["clod_groups_selected"] = words[STAT_CLOD_GROUPS_SELECTED];
	result["clod_groups_visible"] = words[STAT_CLOD_GROUPS_VISIBLE];
	result["clod_triangles_visible"] = words[STAT_CLOD_TRIANGLES_VISIBLE];
	result["hzb_groups_occluded"] = words[STAT_HZB_GROUPS_OCCLUDED];
	result["hzb_triangles_occluded"] = words[STAT_HZB_TRIANGLES_OCCLUDED];
	result["hzb_groups_deferred"] = words[STAT_HZB_GROUPS_DEFERRED];
	result["hzb_triangles_deferred"] = words[STAT_HZB_TRIANGLES_DEFERRED];
	result["hzb_groups_recovered"] = words[STAT_HZB_GROUPS_RECOVERED];
	result["hzb_triangles_recovered"] = words[STAT_HZB_TRIANGLES_RECOVERED];
	result["hzb_instances_occluded"] = words[STAT_HZB_INSTANCES_OCCLUDED];
	result["gpu_instances_occluded"] = words[STAT_GPU_INSTANCES_OCCLUDED];
	result["shadow_multimesh_surfaces_culled"] = p_shadow_culled_surfaces;
	result["shadow_multimesh_instances_tested"] = words[STAT_SHADOW_BASE + STAT_MULTIMESH_INSTANCES_TESTED];
	result["shadow_multimesh_instances_visible"] = words[STAT_SHADOW_BASE + STAT_MULTIMESH_INSTANCES_VISIBLE];
	buffers->geometry_statistics.latest = result;
}

Dictionary RenderForwardClustered::gpu_geometry_get_statistics(const Ref<RenderSceneBuffers> &p_render_buffers) {
	Ref<RenderSceneBuffersRD> render_buffers = p_render_buffers;
	if (render_buffers.is_null() || !render_buffers->has_custom_data(RB_SCOPE_FORWARD_CLUSTERED)) {
		return Dictionary();
	}
	Ref<RenderBufferDataForwardClustered> data = render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	if (data.is_null()) {
		return Dictionary();
	}
	data->geometry_statistics.requested_until_frame = Engine::get_singleton()->get_frames_drawn() + GEOMETRY_STATISTICS_POLL_FRAMES;
	Dictionary result = data->geometry_statistics.latest.duplicate();
	if (!result.is_empty()) {
		// Cluster streaming is global (shared pool), current as of this call.
		result.merge(RendererRD::MeshStorage::get_singleton()->cluster_streaming_get_statistics(), true);
	}
	return result;
}
