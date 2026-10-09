/**************************************************************************/
/*  ray_reconstruction.h                                                  */
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

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "core/templates/hash_map.h"
#include "servers/rendering/renderer_rd/shaders/effects/ray_reconstruction.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

// Native ray reconstruction: a spatiotemporal denoiser for the path tracer,
// built only on compute shaders (no vendor libraries). See
// docs/renderer/native_ray_reconstruction.md.
class RayReconstruction {
public:
	struct Inputs {
		RID base; // Clean part: emission, fog, sky (rgba16f).
		RID diffuse; // Diffuse radiance (rgba16f).
		RID specular; // Specular radiance, a = hit distance (rgba16f).
		RID guide; // Albedos, normal, roughness (rgba32ui).
		RID depth; // NDC depth, 0 for sky (r32f).
		RID velocity; // prev_uv - curr_uv (rg16f).
		RID output; // Internal color texture (rgba16f).
		RID seed; // Random seed key per pixel (r32ui); last frame's until the trace.
		RID gradient_sample; // A-SVGF gradient sample per 3x3 tile (rgba32ui).
		RID gradient_target; // Projected surface point per 3x3 tile (rgba32f).
		Size2i size;
		Projection projection; // As the path tracer's primary rays use it (depth correction and jitter included).
		Projection prev_projection; // Same for the previous frame.
		Projection projection_unjittered; // Depth correction, no jitter.
		Projection prev_projection_unjittered;
		Transform3D cam_transform;
		Transform3D prev_cam_transform;
	};

	RayReconstruction();
	~RayReconstruction();

	// Before the trace: parameters, and the forward projection of last
	// frame's gradient samples (the path tracer replays them).
	void begin_frame(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs);
	// After the trace: gradients, temporal accumulation, filters, compose.
	void process(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs);
	// Drops the per-viewport state (history parity, parameter buffer).
	void free_viewport(RenderSceneBuffersRD *p_render_buffers);

private:
	enum Mode {
		MODE_TEMPORAL,
		MODE_VARIANCE,
		MODE_ATROUS,
		MODE_REFERENCE,
		MODE_GRADIENT_CLEAR,
		MODE_FORWARD_PROJECT,
		MODE_GRADIENT,
		MODE_GRADIENT_FILTER,
		MODE_MAX,
	};

	static constexpr int GRADIENT_FILTER_ITERATIONS = 3;

	enum {
		FLAG_COMPOSE = 2,
		FLAG_RESET = 4,
	};

	enum {
		DEBUG_REFERENCE = 8, // Running average of the input while nothing moves.
	};

	// Matches Params in ray_reconstruction.glsl (std140).
	struct ParamsUBO {
		float inv_projection[16];
		float view_to_world_rotation[16];
		float current_to_previous_view[16];
		float size[4];
		float history[4];
		float filter_params[4];
		float view_ray[4];
		float projection_unjittered[16];
		float previous_projection_unjittered[16];
		float previous_to_current_view[16];
		float previous_view_ray[4];
		float specular_params[4];
		float reserved[8];
	};
	static_assert(sizeof(ParamsUBO) == 512);

	struct PushConstant {
		int32_t step_size;
		int32_t iteration;
		uint32_t flags;
		uint32_t debug_mode;
	};

	struct ViewportState {
		RID params_buffer;
		uint32_t frame = 0;
		// Reference mode: restarts when the camera moves or the mode is entered.
		bool reference_active = false;
		Transform3D reference_cam_transform;
		// The gradient sample buffer last cleared (new buffers start empty).
		RID cleared_gradient_sample;
		bool begun = false; // begin_frame() ran for this frame.
		bool history_valid = false;
	};

	RayReconstructionShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	HashMap<RenderSceneBuffersRD *, ViewportState> viewports;

	RID _get_shader(Mode p_mode);
	bool _ensure_history(Ref<RenderSceneBuffersRD> p_render_buffers);
	void _update_params(ViewportState &r_state, const Inputs &p_inputs, bool p_history_valid);
	void _process_reference(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs, ViewportState &r_state);
};

} // namespace RendererRD
