/**************************************************************************/
/*  planar_reflections.h                                                  */
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

#include "servers/rendering/renderer_rd/shaders/effects/planar_reflections.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_scene_render.h"

#define RB_SCOPE_PLANAR_REFLECTIONS SNAME("rb_planar_reflections")
#define RB_PLANAR_INTERMEDIATE SNAME("intermediate")
#define RB_PLANAR_COLOR SNAME("color")
#define RB_PLANAR_PARAMS SNAME("params")
#define RB_PLANAR_CURRENT SNAME("current")
#define RB_PLANAR_MOTION SNAME("motion")
#define RB_PLANAR_HISTORY SNAME("history")
// The mirror pass's color with mipmaps (blur by roughness), in its own scope:
// it follows the mirror render's size, not the screen's.
#define RB_SCOPE_PLANAR_MIRROR SNAME("rb_planar_mirror")
#define RB_PLANAR_MIRROR_COLOR SNAME("color")

namespace RendererRD {

// Planar reflections by pixel projection (see planar_reflections.glsl): two compute
// passes over the screen per frame, no extra scene rendering. Reflectors come from
// PlanarReflector3D nodes (RenderingServer.planar_reflection_*), culled per camera.
class PlanarReflections {
public:
	static constexpr int MAX_REFLECTORS = 8;

private:
	enum Mode {
		MODE_PROJECT,
		MODE_RESOLVE,
		MODE_TEMPORAL,
		MODE_MAX
	};

	struct ReflectorData {
		float plane[4];
		float center[4];
		float axis_u[4];
		float axis_v[4];
		float params[4];
	};

	struct Params {
		float projection[16];
		float inv_projection[16];
		float reprojection[16];
		int32_t screen_size[2];
		int32_t target_size[2];
		uint32_t reflector_count;
		float max_color_lod;
		uint32_t orthogonal;
		uint32_t history_valid;
		int32_t mirror_reflector;
		float mirror_max_lod;
		uint32_t pad[2];
		ReflectorData reflectors[MAX_REFLECTORS];
	};

	PlanarReflectionsShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
	RID params_buffer;

public:
	PlanarReflections();
	~PlanarReflections();

	// Reflectors in front of the camera, nearest first (the one with a mirror pass
	// before all), at most MAX_REFLECTORS.
	static int select_reflectors(const LocalVector<RendererSceneRender::RenderPlanarReflection> &p_reflectors, const Transform3D &p_cam_transform, const Projection &p_cam_projection, uint32_t p_visible_layers, int p_max, const RendererSceneRender::RenderPlanarReflection **r_selected);

	// Allocates the buffers (full or half size) and runs both passes. p_last_frame is the
	// last frame's color (with mipmaps), p_reprojection maps this frame's clip space to
	// the last frame's. p_mirror_color (mipmapped) and p_mirror_depth are the mirror
	// pass of reflector p_mirror_reflector (-1: none).
	void process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_depth, RID p_normal_roughness, RID p_last_frame, const Projection &p_projection, const Vector2 &p_jitter, const Transform3D &p_cam_transform, const Projection &p_reprojection, const RendererSceneRender::RenderPlanarReflection *const *p_reflectors, int p_count, bool p_half_size, int p_mirror_reflector = -1, RID p_mirror_color = RID(), RID p_mirror_depth = RID());
};

} // namespace RendererRD
