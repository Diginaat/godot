/**************************************************************************/
/*  ray_reconstruction.cpp                                                */
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

#include "ray_reconstruction.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_default.h" // IWYU pragma: keep. RENDER_TIMESTAMP macro uses RSG.

using namespace RendererRD;

RayReconstruction::RayReconstruction() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_COMPOSE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

RayReconstruction::~RayReconstruction() {
	shader.version_free(shader_version);
}

RID RayReconstruction::_get_shader(Mode p_mode) {
	return shader.version_get_shader(shader_version, p_mode);
}

void RayReconstruction::process(Ref<RenderSceneBuffersRD> p_render_buffers, const Inputs &p_inputs) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	RD *rd = RD::get_singleton();

	PushConstant pc = {};
	pc.size[0] = p_inputs.size.x;
	pc.size[1] = p_inputs.size.y;
	pc.debug_mode = uint32_t(int(GLOBAL_GET_CACHED(int, "rendering/ray_reconstruction/debug_mode")));

	rd->draw_command_begin_label("Ray Reconstruction");
	RENDER_TIMESTAMP("RR Compose");

	RID compose_shader = _get_shader(MODE_COMPOSE);
	RD::ComputeListID compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_COMPOSE]);
	RID set = uniform_set_cache->get_cache(compose_shader, 0,
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_inputs.base),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_inputs.diffuse),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, p_inputs.specular),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, p_inputs.output));
	rd->compute_list_bind_uniform_set(compute_list, set, 0);
	rd->compute_list_set_push_constant(compute_list, &pc, sizeof(PushConstant));
	rd->compute_list_dispatch_threads(compute_list, p_inputs.size.x, p_inputs.size.y, 1);
	rd->compute_list_end();

	RENDER_TIMESTAMP("RR Done");
	rd->draw_command_end_label();
}
