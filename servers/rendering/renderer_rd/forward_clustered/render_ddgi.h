/**************************************************************************/
/*  render_ddgi.h                                                         */
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

#pragma once

#include "core/math/random_pcg.h"
#include "core/math/vector3i.h"
#include "servers/rendering/renderer_rd/shaders/raytracing/ddgi_apply.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/raytracing/ddgi_update.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_buffer_custom_data_rd.h"
#include "servers/rendering/rendering_device.h"

#define RB_SCOPE_DDGI SNAME("ddgi")

class RenderDataRD;
class RenderSceneBuffersRD;

namespace RendererSceneRenderImplementation {

class RenderRaytracing;
struct RTViewportState;

// GPU data layouts, shared with shaders/raytracing/ddgi_inc.glsl.

// std140, 160 bytes.
struct DDGIVolumeGPU {
	float world_to_local[12]; // Rows of a 3x4 affine transform.
	float local_to_world[12];
	int32_t grid[4]; // xyz: probes per axis, w: global index of the first probe.
	int32_t scroll[4]; // xyz: storage offset, w: flags.
	float spacing[4]; // x: spacing, y: normal bias, z: view bias, w: max ray distance.
	float params[4]; // x: energy, y: hysteresis, z: update weight, w: edge blend (probes).
};
static_assert(sizeof(DDGIVolumeGPU) == 160, "DDGIVolumeGPU must match the std140 layout of DDGIVolume");

// std140.
struct DDGIDataGPU {
	DDGIVolumeGPU volumes[8];
	int32_t volume_reset[8][4];
	float ray_rotation[12];
	float camera_view_projection[16];
	float camera_position[4];
	uint32_t counts[4]; // Volumes, rays per probe, fixed rays per probe, update capacity.
	uint32_t atlas[4]; // Irradiance texels, distance texels, probes per row, frame.
	float atlas_inv_size[4];
	float schedule[4]; // Base update rate, total probes, max radiance per ray, unused.
	float miss_color[4];
};
static_assert(sizeof(DDGIDataGPU) == 1616, "DDGIDataGPU must match the std140 layout of DDGIDataBlock");

// std430, 32 bytes.
struct DDGIProbeGPU {
	float offset[3];
	uint32_t state;
	float urgency;
	float variability;
	uint32_t last_update_frame;
	float luminance;
};
static_assert(sizeof(DDGIProbeGPU) == 32, "DDGIProbeGPU must match DDGIProbe");

/// Resources the ray tracing scene set binds at 34-39 (raytracing_ddgi_inc.glsl).
struct RTDDGIBindings {
	RID uniform_buffer;
	RID irradiance_atlas;
	RID distance_atlas;
	RID probe_buffer;
	RID ray_data;
	RID update_list;
	/// True when the dispatch traces probe rays instead of camera paths.
	bool trace = false;
};

/// Dynamic diffuse global illumination: ray traced irradiance probes for the
/// Forward+ rasterizer. See DDGI.md in the repository root.
///
/// The probes are traced with the path tracer's ray tracing pipeline (same
/// TLAS, materials, custom shader hit groups, lights and sky); the raygen
/// shader switches to probe rays when RT_PARAM_DDGI_TRACE is set. The result
/// reaches the scene shader through the GI ambient buffer, like SDFGI.
class RenderDDGI {
public:
	enum {
		MAX_VOLUMES = 8,
		MAX_CASCADES = 4,
	};

	enum DebugMode {
		DEBUG_DISABLED,
		DEBUG_INDIRECT_LIGHT,
		DEBUG_PROBE_IRRADIANCE,
		DEBUG_PROBE_DISTANCE,
		DEBUG_PROBE_STATES,
		DEBUG_PROBE_PRIORITY,
		DEBUG_CASCADES,
	};

	/// Workload settings from rendering/global_illumination/ddgi/*.
	struct Quality {
		uint32_t rays_per_probe = 128;
		uint32_t probes_per_frame = 2048;
		uint32_t irradiance_texels = 6;
		uint32_t distance_texels = 14;
		float gpu_time_budget_ms = 0.0;
	};
	static Quality get_quality();

	/// Per-viewport probe state (each camera has its own volumes).
	class ViewportData : public RenderBufferCustomDataRD {
		GDCLASS(ViewportData, RenderBufferCustomDataRD)

	public:
		// What the resources were created for; a change reallocates them.
		int cascades = 0;
		Vector3i grid;
		Quality quality;
		uint32_t capacity = 0;

		uint32_t total_probes = 0;
		uint32_t probes_per_row = 0;
		Size2i irradiance_size;
		Size2i distance_size;

		RID data_buffer;
		RID probe_buffer;
		RID update_list;
		RID ray_data;
		RID irradiance_atlas;
		RID distance_atlas;

		struct VolumeState {
			Vector3i origin; // World probe index of logical probe (0,0,0).
			Vector3i scroll;
			float spacing = 0.0;
			bool valid = false;
		};
		VolumeState volumes[MAX_VOLUMES];

		uint32_t frame = 0;
		uint32_t update_capacity_used = 0; // Probes the trace dispatch covers (budget control).
		RandomPCG rng;

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override {}
		virtual void free_data() override;
	};

private:
	enum UpdateMode {
		UPDATE_SCHEDULE,
		UPDATE_BLEND_IRRADIANCE,
		UPDATE_BLEND_DISTANCE,
		UPDATE_RELOCATE_CLASSIFY,
		UPDATE_MODE_MAX,
	};

	enum ApplyMode {
		APPLY_GI,
		APPLY_DEBUG_PROBES,
		APPLY_MODE_MAX,
	};

	struct UpdatePushConstant {
		uint32_t total_probes;
		uint32_t pad[3];
	};

	struct ApplyPushConstant {
		float inv_projection[16];
		float cam_rotation[12];
		int32_t screen_size[2];
		float energy;
		uint32_t debug_mode;
	};
	static_assert(sizeof(ApplyPushConstant) == 128, "Push constants are limited to 128 bytes");

	DdgiUpdateShaderRD update_shader;
	RID update_shader_version;
	RID update_pipelines[UPDATE_MODE_MAX];

	DdgiApplyShaderRD apply_shader;
	RID apply_shader_version;
	RID apply_pipelines[APPLY_MODE_MAX];

	bool initialized = false;

	Ref<ViewportData> _get_viewport_data(RenderSceneBuffersRD *p_render_buffers);
	void _allocate(ViewportData *p_data, int p_cascades, const Vector3i &p_grid, const Quality &p_quality);
	uint32_t _setup_volumes(ViewportData *p_data, const RenderDataRD *p_render_data, DDGIDataGPU &r_gpu);

public:
	void initialize();

	/// True when DDGI should run for this view (enabled, Forward+ RT available,
	/// not a reflection probe, path tracing off).
	static bool is_enabled_for(const RenderDataRD *p_render_data);

	/// Updates the probes: schedule, trace (with the given RT scene uniform
	/// set setup callback), blend, relocate and classify. Called after the TLAS
	/// is built for this frame.
	void update_probes(RenderDataRD *p_render_data, RenderRaytracing *p_raytracing, RTViewportState *p_rt_state, uint32_t p_rt_flags);

	/// Writes the diffuse indirect light into the GI ambient buffer.
	void apply(RenderDataRD *p_render_data, const RID *p_normal_roughness_slices);

	/// Debug view of the probes over the internal color buffer (before tonemapping).
	void debug_draw(RenderDataRD *p_render_data);

	/// GI ambient buffer for the "Indirect Light" and "Cascades" debug views.
	static bool debug_shows_gi_buffer(const RenderDataRD *p_render_data);

	~RenderDDGI();
};

} // namespace RendererSceneRenderImplementation
