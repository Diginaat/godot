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
		Size2i size;
	};

	RayReconstruction();
	~RayReconstruction();

	void process(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs);

private:
	enum Mode {
		MODE_COMPOSE,
		MODE_MAX,
	};

	struct PushConstant {
		int32_t size[2];
		uint32_t debug_mode;
		uint32_t pad;
	};

	RayReconstructionShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];

	RID _get_shader(Mode p_mode);
};

} // namespace RendererRD
