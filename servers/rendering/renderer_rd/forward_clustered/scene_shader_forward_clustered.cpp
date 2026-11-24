/**************************************************************************/
/*  scene_shader_forward_clustered.cpp                                    */
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

#include "scene_shader_forward_clustered.h"

#include "core/config/project_settings.h"
#include "core/math/math_defs.h"
#include "core/os/os.h"
#include "servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

using namespace RendererSceneRenderImplementation;

void SceneShaderForwardClustered::ShaderData::set_code(const String &p_code) {
	//compile

	code = p_code;
	ubo_size = 0;
	uniforms.clear();
	_clear_vertex_input_mask_cache();

	if (code.is_empty()) {
		return; //just invalid, but no error
	}

	ShaderCompiler::GeneratedCode gen_code;

	blend_mode = BLEND_MODE_MIX;
	depth_test_disabledi = 0;
	depth_test_invertedi = 0;
	alpha_antialiasing_mode = ALPHA_ANTIALIASING_OFF;
	int cull_modei = RSE::CULL_MODE_BACK;

	uses_point_size = false;
	uses_alpha = false;
	uses_alpha_clip = false;
	uses_alpha_antialiasing = false;
	uses_blend_alpha = false;
	uses_depth_prepass_alpha = false;
	uses_discard = false;
	uses_roughness = false;
	uses_normal = false;
	uses_tangent = false;
	writes_tangent = false;
	uses_normal_map = false;
	uses_bent_normal_map = false;
	wireframe = false;

	unshaded = false;
	uses_vertex = false;
	uses_position = false;
	uses_sss = false;
	uses_transmittance = false;
	uses_time = false;
	writes_modelview_or_projection = false;
	uses_custom_vertex_transform = false;
	writes_fragment_depth = false;
	uses_world_coordinates = false;
	uses_particle_trails = false;
	uses_z_clip_scale = false;

	int depth_drawi = DEPTH_DRAW_OPAQUE;

	int stencil_readi = 0;
	int stencil_writei = 0;
	int stencil_write_depth_faili = 0;
	int stencil_comparei = STENCIL_COMPARE_ALWAYS;
	int stencil_referencei = -1;

	ShaderCompiler::IdentifierActions actions;
	actions.entry_point_stages["vertex"] = ShaderCompiler::STAGE_VERTEX;
	actions.entry_point_stages["fragment"] = ShaderCompiler::STAGE_FRAGMENT;
	actions.entry_point_stages["light"] = ShaderCompiler::STAGE_FRAGMENT;

	actions.render_mode_values["blend_add"] = Pair<int *, int>(&blend_mode, BLEND_MODE_ADD);
	actions.render_mode_values["blend_mix"] = Pair<int *, int>(&blend_mode, BLEND_MODE_MIX);
	actions.render_mode_values["blend_sub"] = Pair<int *, int>(&blend_mode, BLEND_MODE_SUB);
	actions.render_mode_values["blend_mul"] = Pair<int *, int>(&blend_mode, BLEND_MODE_MUL);
	actions.render_mode_values["blend_premul_alpha"] = Pair<int *, int>(&blend_mode, BLEND_MODE_PREMULTIPLIED_ALPHA);

	actions.render_mode_values["alpha_to_coverage"] = Pair<int *, int>(&alpha_antialiasing_mode, ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
	actions.render_mode_values["alpha_to_coverage_and_one"] = Pair<int *, int>(&alpha_antialiasing_mode, ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE_AND_TO_ONE);

	actions.render_mode_values["depth_draw_never"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_DISABLED);
	actions.render_mode_values["depth_draw_opaque"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_OPAQUE);
	actions.render_mode_values["depth_draw_always"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_ALWAYS);

	actions.render_mode_values["depth_test_disabled"] = Pair<int *, int>(&depth_test_disabledi, 1);
	actions.render_mode_values["depth_test_inverted"] = Pair<int *, int>(&depth_test_invertedi, 1);

	actions.render_mode_values["cull_disabled"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_DISABLED);
	actions.render_mode_values["cull_front"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_FRONT);
	actions.render_mode_values["cull_back"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_BACK);

	actions.render_mode_flags["unshaded"] = &unshaded;
	actions.render_mode_flags["wireframe"] = &wireframe;
	actions.render_mode_flags["particle_trails"] = &uses_particle_trails;
	actions.render_mode_flags["world_vertex_coords"] = &uses_world_coordinates;
	actions.render_mode_flags["skip_vertex_transform"] = &uses_custom_vertex_transform;

	actions.usage_flag_pointers["ALPHA"] = &uses_alpha;
	actions.usage_flag_pointers["ALPHA_SCISSOR_THRESHOLD"] = &uses_alpha_clip;
	actions.usage_flag_pointers["ALPHA_HASH_SCALE"] = &uses_alpha_clip;
	actions.usage_flag_pointers["ALPHA_ANTIALIASING_EDGE"] = &uses_alpha_antialiasing;
	actions.usage_flag_pointers["ALPHA_TEXTURE_COORDINATE"] = &uses_alpha_antialiasing;
	actions.render_mode_flags["depth_prepass_alpha"] = &uses_depth_prepass_alpha;

	actions.usage_flag_pointers["SSS_STRENGTH"] = &uses_sss;
	actions.usage_flag_pointers["SSS_TRANSMITTANCE_DEPTH"] = &uses_transmittance;

	actions.usage_flag_pointers["DISCARD"] = &uses_discard;
	actions.usage_flag_pointers["TIME"] = &uses_time;
	actions.usage_flag_pointers["ROUGHNESS"] = &uses_roughness;
	actions.usage_flag_pointers["NORMAL"] = &uses_normal;
	actions.usage_flag_pointers["NORMAL_MAP"] = &uses_normal_map;
	actions.usage_flag_pointers["BENT_NORMAL_MAP"] = &uses_bent_normal_map;

	actions.usage_flag_pointers["POINT_SIZE"] = &uses_point_size;
	actions.usage_flag_pointers["POINT_COORD"] = &uses_point_size;

	actions.usage_flag_pointers["TANGENT"] = &uses_tangent;
	actions.usage_flag_pointers["BINORMAL"] = &uses_tangent;
	actions.usage_flag_pointers["ANISOTROPY"] = &uses_tangent;
	actions.usage_flag_pointers["ANISOTROPY_FLOW"] = &uses_tangent;

	actions.write_flag_pointers["MODELVIEW_MATRIX"] = &writes_modelview_or_projection;
	actions.write_flag_pointers["MODEL_MATRIX"] = &uses_custom_vertex_transform;
	actions.write_flag_pointers["PROJECTION_MATRIX"] = &writes_modelview_or_projection;
	actions.write_flag_pointers["VERTEX"] = &uses_vertex;
	actions.write_flag_pointers["POSITION"] = &uses_position;
	actions.write_flag_pointers["DEPTH"] = &writes_fragment_depth;
	actions.write_flag_pointers["TANGENT"] = &writes_tangent;
	actions.write_flag_pointers["BINORMAL"] = &writes_tangent;
	actions.write_flag_pointers["Z_CLIP_SCALE"] = &uses_z_clip_scale;

	actions.stencil_mode_values["read"] = Pair<int *, int>(&stencil_readi, STENCIL_FLAG_READ);
	actions.stencil_mode_values["write"] = Pair<int *, int>(&stencil_writei, STENCIL_FLAG_WRITE);
	actions.stencil_mode_values["write_depth_fail"] = Pair<int *, int>(&stencil_write_depth_faili, STENCIL_FLAG_WRITE_DEPTH_FAIL);

	actions.stencil_mode_values["compare_less"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_LESS);
	actions.stencil_mode_values["compare_equal"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_EQUAL);
	actions.stencil_mode_values["compare_less_or_equal"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_LESS_OR_EQUAL);
	actions.stencil_mode_values["compare_greater"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_GREATER);
	actions.stencil_mode_values["compare_not_equal"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_NOT_EQUAL);
	actions.stencil_mode_values["compare_greater_or_equal"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_GREATER_OR_EQUAL);
	actions.stencil_mode_values["compare_always"] = Pair<int *, int>(&stencil_comparei, STENCIL_COMPARE_ALWAYS);

	actions.stencil_reference = &stencil_referencei;

	actions.uniforms = &uniforms;

	Error err = OK;
	{
		MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
		err = SceneShaderForwardClustered::singleton->compiler.compile(RSE::SHADER_SPATIAL, code, &actions, path, gen_code);
	}

	if (err != OK) {
		if (version.is_valid()) {
			SceneShaderForwardClustered::singleton->shader.version_free(version);
			version = RID();
		}
		ERR_FAIL_MSG("Shader compilation failed.");
	}

	if (version.is_null()) {
		version = SceneShaderForwardClustered::singleton->shader.version_create(false);
	}

	depth_draw = DepthDraw(depth_drawi);
	if (depth_test_disabledi) {
		depth_test = DEPTH_TEST_DISABLED;
	} else if (depth_test_invertedi) {
		depth_test = DEPTH_TEST_ENABLED_INVERTED;
	} else {
		depth_test = DEPTH_TEST_ENABLED;
	}
	cull_mode = RSE::CullMode(cull_modei);
	uses_screen_texture_mipmaps = gen_code.uses_screen_texture_mipmaps;
	uses_screen_texture = gen_code.uses_screen_texture;
	uses_depth_texture = gen_code.uses_depth_texture;
	uses_normal_texture = gen_code.uses_normal_roughness_texture;
	uses_vertex_time = gen_code.uses_vertex_time;
	uses_fragment_time = gen_code.uses_fragment_time;
	uses_normal |= uses_normal_map;
	uses_normal |= uses_bent_normal_map;
	uses_tangent |= uses_normal_map;
	uses_tangent |= uses_bent_normal_map;

	stencil_enabled = stencil_referencei != -1;
	stencil_flags = stencil_readi | stencil_writei | stencil_write_depth_faili;
	stencil_compare = StencilCompare(stencil_comparei);
	stencil_reference = stencil_referencei;

#if 0
	print_line("**compiling shader:");
	print_line("**defines:\n");
	for (int i = 0; i < gen_code.defines.size(); i++) {
		print_line(gen_code.defines[i]);
	}

	HashMap<String, String>::Iterator el = gen_code.code.begin();
	while (el) {
		print_line("\n**code " + el->key + ":\n" + el->value);
		++el;
	}

	print_line("\n**uniforms:\n" + gen_code.uniforms);
	print_line("\n**vertex_globals:\n" + gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX]);
	print_line("\n**fragment_globals:\n" + gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT]);
#endif
	pipeline_hash_map.clear_pipelines();

	// If any form of Alpha Antialiasing is enabled, set the blend mode to alpha to coverage.
	if (alpha_antialiasing_mode != ALPHA_ANTIALIASING_OFF) {
		blend_mode = BLEND_MODE_ALPHA_TO_COVERAGE;
	}

	uses_blend_alpha = blend_mode_uses_blend_alpha(BlendMode(blend_mode));

	// Variants this shader can't draw with, compiled only if something asks (see
	// _geometry_instance_add_surface_with_material and _fill_render_list for the
	// pass routing):
	// - plain depth: depth and shadow passes draw shaders that use the shared
	//   shadow material with the default material's shader;
	// - every depth variant but the material one: a shader only in the alpha pass
	//   without depth is in no depth, shadow, normal-roughness or SDF list;
	// - color with motion vectors or separate specular: a shader in the alpha pass
	//   is drawn only by the transparent passes, which use neither.
	uint64_t skipped_variants = 0;
	if (!SceneShaderForwardClustered::singleton->initializing_default_shader) {
		const bool alpha_only = uses_alpha_pass();
		const bool no_depth = alpha_only && !uses_depth_in_alpha_pass();
		for (uint32_t ubershader = 0; ubershader < 2; ubershader++) {
			const uint32_t base = ubershader ? ShaderVersion::SHADER_VERSION_COLOR_PASS : 0;
			if (uses_shared_shadow_material() || no_depth) {
				skipped_variants |= uint64_t(1) << (base + ShaderVersion::SHADER_VERSION_DEPTH_PASS);
				skipped_variants |= uint64_t(1) << (base + ShaderVersion::SHADER_VERSION_DEPTH_PASS_DP);
				skipped_variants |= uint64_t(1) << (base + ShaderVersion::SHADER_VERSION_DEPTH_PASS_MULTIVIEW);
			}
			if (no_depth) {
				for (uint32_t depth_version = 0; depth_version < ShaderVersion::SHADER_VERSION_COLOR_PASS; depth_version++) {
					if (depth_version != ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_MATERIAL) {
						skipped_variants |= uint64_t(1) << (base + depth_version);
					}
				}
			}
		}
		if (alpha_only) {
			for (uint32_t flags = 0; flags < SHADER_COLOR_PASS_FLAG_COUNT; flags++) {
				if (flags & (SHADER_COLOR_PASS_FLAG_SEPARATE_SPECULAR | SHADER_COLOR_PASS_FLAG_MOTION_VECTORS)) {
					skipped_variants |= uint64_t(1) << (ShaderVersion::SHADER_VERSION_COLOR_PASS * 2 + flags);
				}
			}
		}
	}
	SceneShaderForwardClustered::singleton->shader.version_set_skipped_variants(version, skipped_variants);

	SceneShaderForwardClustered::singleton->shader.version_set_code(version, gen_code.code, gen_code.uniforms, gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX], gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT], gen_code.defines);

	ubo_size = gen_code.uniform_total_size;
	ubo_offsets = gen_code.uniform_offsets;
	texture_uniforms = gen_code.texture_uniforms;
	if (!material_table || material_table->stride != ubo_size) {
		// Materials move to the new table at their next update (set_code
		// queues them all).
		MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
		MaterialTable::unref(material_table);
		material_table = memnew(MaterialTable);
		material_table->stride = ubo_size;
	}
}

void SceneShaderForwardClustered::ShaderData::set_code_rt(const String &p_code_rt) {
	// No `#if defined(RT)` divergence
	if (p_code_rt.is_empty() || p_code_rt == code) {
		if (rt) {
			memdelete(rt);
			rt = nullptr;
		}
		return;
	}

	// Flag-extraction-only pass; raster pipeline state is untouched.
	int blend_modei = BLEND_MODE_MIX;
	int depth_drawi = DEPTH_DRAW_OPAQUE;
	int depth_test_disabledi_local = 0;
	int depth_test_invertedi_local = 0;
	int alpha_antialiasing_modei = ALPHA_ANTIALIASING_OFF;
	int cull_modei = RSE::CULL_MODE_BACK;

	bool local_uses_alpha = false;
	bool local_uses_alpha_clip = false;
	bool local_uses_alpha_antialiasing = false;
	bool local_uses_depth_prepass_alpha = false;

	ShaderCompiler::IdentifierActions actions;
	actions.entry_point_stages["vertex"] = ShaderCompiler::STAGE_VERTEX;
	actions.entry_point_stages["fragment"] = ShaderCompiler::STAGE_FRAGMENT;
	actions.entry_point_stages["light"] = ShaderCompiler::STAGE_FRAGMENT;

	// Only the inputs that influence `rt_uses_alpha_pass` / `rt_uses_depth_in_alpha_pass`.
	actions.render_mode_values["blend_add"] = Pair<int *, int>(&blend_modei, BLEND_MODE_ADD);
	actions.render_mode_values["blend_mix"] = Pair<int *, int>(&blend_modei, BLEND_MODE_MIX);
	actions.render_mode_values["blend_sub"] = Pair<int *, int>(&blend_modei, BLEND_MODE_SUB);
	actions.render_mode_values["blend_mul"] = Pair<int *, int>(&blend_modei, BLEND_MODE_MUL);
	actions.render_mode_values["blend_premul_alpha"] = Pair<int *, int>(&blend_modei, BLEND_MODE_PREMULTIPLIED_ALPHA);

	actions.render_mode_values["alpha_to_coverage"] = Pair<int *, int>(&alpha_antialiasing_modei, ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
	actions.render_mode_values["alpha_to_coverage_and_one"] = Pair<int *, int>(&alpha_antialiasing_modei, ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE_AND_TO_ONE);

	actions.render_mode_values["depth_draw_never"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_DISABLED);
	actions.render_mode_values["depth_draw_opaque"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_OPAQUE);
	actions.render_mode_values["depth_draw_always"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_ALWAYS);

	actions.render_mode_values["depth_test_disabled"] = Pair<int *, int>(&depth_test_disabledi_local, 1);
	actions.render_mode_values["depth_test_inverted"] = Pair<int *, int>(&depth_test_invertedi_local, 1);

	actions.render_mode_values["cull_disabled"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_DISABLED);
	actions.render_mode_values["cull_front"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_FRONT);
	actions.render_mode_values["cull_back"] = Pair<int *, int>(&cull_modei, RSE::CULL_MODE_BACK);

	actions.render_mode_flags["depth_prepass_alpha"] = &local_uses_depth_prepass_alpha;

	actions.usage_flag_pointers["ALPHA"] = &local_uses_alpha;
	actions.usage_flag_pointers["ALPHA_SCISSOR_THRESHOLD"] = &local_uses_alpha_clip;
	actions.usage_flag_pointers["ALPHA_HASH_SCALE"] = &local_uses_alpha_clip;
	actions.usage_flag_pointers["ALPHA_ANTIALIASING_EDGE"] = &local_uses_alpha_antialiasing;
	actions.usage_flag_pointers["ALPHA_TEXTURE_COORDINATE"] = &local_uses_alpha_antialiasing;

	HashMap<StringName, ShaderLanguage::ShaderNode::Uniform> rt_uniform_sink;
	actions.uniforms = &rt_uniform_sink;

	ShaderCompiler::GeneratedCode rt_gen_code;
	Error err = OK;
	{
		MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
		err = SceneShaderForwardClustered::singleton->compiler.compile(RSE::SHADER_SPATIAL, p_code_rt, &actions, path, rt_gen_code);
	}

	if (err != OK) {
		// RT compile failed: drop any prior divergence -> fall back to raster.
		if (rt) {
			memdelete(rt);
			rt = nullptr;
		}
		WARN_PRINT(vformat("Forward Clustered: RT-variant classification compile failed for shader %s. Falling back to raster-side flags for ray-tracing routing.", path.is_empty() ? String("<inline>") : path));
		return;
	}

	if (alpha_antialiasing_modei != ALPHA_ANTIALIASING_OFF) {
		blend_modei = BLEND_MODE_ALPHA_TO_COVERAGE;
	}

	if (!rt) {
		rt = memnew(RTClassification);
	}
	rt->code = p_code_rt;
	rt->blend_mode = blend_modei;
	rt->alpha_antialiasing_mode = alpha_antialiasing_modei;
	rt->depth_draw = DepthDraw(depth_drawi);
	rt->cull_mode = RSE::CullMode(cull_modei);
	if (depth_test_disabledi_local) {
		rt->depth_test = DEPTH_TEST_DISABLED;
	} else if (depth_test_invertedi_local) {
		rt->depth_test = DEPTH_TEST_ENABLED_INVERTED;
	} else {
		rt->depth_test = DEPTH_TEST_ENABLED;
	}

	rt->uses_alpha = local_uses_alpha;
	rt->uses_alpha_clip = local_uses_alpha_clip;
	rt->uses_alpha_antialiasing = local_uses_alpha_antialiasing;
	rt->uses_depth_prepass_alpha = local_uses_depth_prepass_alpha;
	rt->uses_blend_alpha = blend_mode_uses_blend_alpha(BlendMode(rt->blend_mode));

	rt->uses_screen_texture = rt_gen_code.uses_screen_texture;
	rt->uses_depth_texture = rt_gen_code.uses_depth_texture;
	rt->uses_normal_texture = rt_gen_code.uses_normal_roughness_texture;
}

bool SceneShaderForwardClustered::ShaderData::is_animated() const {
	return (uses_fragment_time && uses_discard) || (uses_vertex_time && uses_vertex);
}

bool SceneShaderForwardClustered::ShaderData::casts_shadows() const {
	bool has_read_screen_alpha = uses_screen_texture || uses_depth_texture || uses_normal_texture;
	bool has_base_alpha = (uses_alpha && (!uses_alpha_clip || uses_alpha_antialiasing)) || has_read_screen_alpha;
	bool has_alpha = has_base_alpha || uses_blend_alpha;

	return !has_alpha || (uses_depth_prepass_alpha && !(depth_draw == DEPTH_DRAW_DISABLED || depth_test != DEPTH_TEST_ENABLED));
}

RenderingServerTypes::ShaderNativeSourceCode SceneShaderForwardClustered::ShaderData::get_native_source_code() const {
	if (version.is_valid()) {
		return SceneShaderForwardClustered::singleton->shader.version_get_native_source_code(version);
	} else {
		return RenderingServerTypes::ShaderNativeSourceCode();
	}
}

Pair<ShaderRD *, RID> SceneShaderForwardClustered::ShaderData::get_native_shader_and_version() const {
	if (version.is_valid()) {
		return { &SceneShaderForwardClustered::singleton->shader, version };
	} else {
		return {};
	}
}

uint16_t SceneShaderForwardClustered::ShaderData::_get_shader_version(PipelineVersion p_pipeline_version, uint32_t p_color_pass_flags, bool p_ubershader) const {
	uint32_t ubershader_base = p_ubershader ? ShaderVersion::SHADER_VERSION_COLOR_PASS : 0;
	switch (p_pipeline_version) {
		case PIPELINE_VERSION_DEPTH_PASS:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_DP:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_DP + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_MULTIVIEW:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_MULTIVIEW + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_MULTIVIEW:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_MULTIVIEW + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI_MULTIVIEW:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI_MULTIVIEW + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_MATERIAL:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_MATERIAL + ubershader_base;
		case PIPELINE_VERSION_DEPTH_PASS_WITH_SDF:
			return ShaderVersion::SHADER_VERSION_DEPTH_PASS_WITH_SDF + ubershader_base;
		case PIPELINE_VERSION_COLOR_PASS: {
			int shader_flags = 0;

			if (p_ubershader) {
				shader_flags |= SHADER_COLOR_PASS_FLAG_UBERSHADER;
			}

			if (p_color_pass_flags & PIPELINE_COLOR_PASS_FLAG_SEPARATE_SPECULAR) {
				shader_flags |= SHADER_COLOR_PASS_FLAG_SEPARATE_SPECULAR;
			}

			if (p_color_pass_flags & PIPELINE_COLOR_PASS_FLAG_MOTION_VECTORS) {
				shader_flags |= SHADER_COLOR_PASS_FLAG_MOTION_VECTORS;
			}

			if (p_color_pass_flags & PIPELINE_COLOR_PASS_FLAG_LIGHTMAP) {
				shader_flags |= SHADER_COLOR_PASS_FLAG_LIGHTMAP;
			}

			if (p_color_pass_flags & PIPELINE_COLOR_PASS_FLAG_MULTIVIEW) {
				shader_flags |= SHADER_COLOR_PASS_FLAG_MULTIVIEW;
			}

			return ShaderVersion::SHADER_VERSION_COLOR_PASS * 2 + shader_flags;
		} break;
		default: {
			DEV_ASSERT(false && "Unknown pipeline version.");
			return 0;
		} break;
	}
}

void SceneShaderForwardClustered::ShaderData::_create_pipeline(PipelineKey p_pipeline_key) {
#if PRINT_PIPELINE_COMPILATION_KEYS
	print_line(
			"HASH:", p_pipeline_key.hash(),
			"VERSION:", version,
			"VERTEX:", p_pipeline_key.vertex_format_id,
			"FRAMEBUFFER:", p_pipeline_key.framebuffer_format_id,
			"CULL:", p_pipeline_key.cull_mode,
			"PRIMITIVE:", p_pipeline_key.primitive_type,
			"VERSION:", p_pipeline_key.version,
			"PASS FLAGS:", p_pipeline_key.color_pass_flags,
			"SPEC PACKED #0:", p_pipeline_key.shader_specialization.packed_0,
			"WIREFRAME:", p_pipeline_key.wireframe);
#endif

	// Color pass -> attachment 0: Color/Diffuse, attachment 1: Separate Specular, attachment 2: Motion Vectors
	RD::PipelineColorBlendState::Attachment blend_attachment = blend_mode_to_blend_attachment(BlendMode(blend_mode));
	RD::PipelineColorBlendState blend_state_color_blend;
	blend_state_color_blend.attachments = { blend_attachment, RD::PipelineColorBlendState::Attachment(), RD::PipelineColorBlendState::Attachment() };
	RD::PipelineColorBlendState blend_state_color_opaque = RD::PipelineColorBlendState::create_disabled(3);
	RD::PipelineColorBlendState blend_state_depth_normal_roughness = RD::PipelineColorBlendState::create_disabled(1);
	RD::PipelineColorBlendState blend_state_depth_normal_roughness_giprobe = RD::PipelineColorBlendState::create_disabled(2);

	RD::PipelineDepthStencilState depth_stencil_state;

	if (depth_test != DEPTH_TEST_DISABLED) {
		depth_stencil_state.enable_depth_test = true;
		depth_stencil_state.enable_depth_write = depth_draw != DEPTH_DRAW_DISABLED ? true : false;
		depth_stencil_state.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;

		if (depth_test == DEPTH_TEST_ENABLED_INVERTED) {
			depth_stencil_state.depth_compare_operator = RD::COMPARE_OP_LESS;
		}
	}

	bool use_stencil = stencil_enabled && p_pipeline_key.version == PIPELINE_VERSION_COLOR_PASS;
	depth_stencil_state.enable_stencil = use_stencil;
	if (use_stencil) {
		static const RD::CompareOperator stencil_compare_rd_table[STENCIL_COMPARE_MAX] = {
			RD::COMPARE_OP_LESS,
			RD::COMPARE_OP_EQUAL,
			RD::COMPARE_OP_LESS_OR_EQUAL,
			RD::COMPARE_OP_GREATER,
			RD::COMPARE_OP_NOT_EQUAL,
			RD::COMPARE_OP_GREATER_OR_EQUAL,
			RD::COMPARE_OP_ALWAYS,
		};

		uint32_t stencil_mask = 255;

		RD::PipelineDepthStencilState::StencilOperationState op;
		op.fail = RD::STENCIL_OP_KEEP;
		op.pass = RD::STENCIL_OP_KEEP;
		op.depth_fail = RD::STENCIL_OP_KEEP;
		op.compare = stencil_compare_rd_table[stencil_compare];
		op.compare_mask = 0;
		op.write_mask = 0;
		op.reference = stencil_reference;

		if (stencil_flags & STENCIL_FLAG_READ) {
			op.compare_mask = stencil_mask;
		}

		if (stencil_flags & STENCIL_FLAG_WRITE) {
			op.pass = RD::STENCIL_OP_REPLACE;
			op.write_mask = stencil_mask;
		}

		if (stencil_flags & STENCIL_FLAG_WRITE_DEPTH_FAIL) {
			op.depth_fail = RD::STENCIL_OP_REPLACE;
			op.write_mask = stencil_mask;
		}

		depth_stencil_state.front_op = op;
		depth_stencil_state.back_op = op;
	}

	const bool depth_pre_pass_enabled = SceneShaderForwardClustered::singleton->depth_prepass_enabled;

	RD::RenderPrimitive primitive_rd_table[RSE::PRIMITIVE_MAX] = {
		RD::RENDER_PRIMITIVE_POINTS,
		RD::RENDER_PRIMITIVE_LINES,
		RD::RENDER_PRIMITIVE_LINESTRIPS,
		RD::RENDER_PRIMITIVE_TRIANGLES,
		RD::RENDER_PRIMITIVE_TRIANGLE_STRIPS,
	};

	bool emulate_point_size_flag = uses_point_size && SceneShaderForwardClustered::singleton->emulate_point_size;

	RD::RenderPrimitive primitive_rd;
	if (uses_point_size) {
		primitive_rd = emulate_point_size_flag ? RD::RENDER_PRIMITIVE_TRIANGLES : RD::RENDER_PRIMITIVE_POINTS;
	} else {
		primitive_rd = primitive_rd_table[p_pipeline_key.primitive_type];
	}

	RD::PipelineRasterizationState raster_state;
	raster_state.cull_mode = p_pipeline_key.cull_mode;
	raster_state.wireframe = wireframe || p_pipeline_key.wireframe;

	RD::PipelineMultisampleState multisample_state;
	multisample_state.sample_count = RD::get_singleton()->framebuffer_format_get_texture_samples(p_pipeline_key.framebuffer_format_id, 0);

	RD::PipelineColorBlendState blend_state;
	if (p_pipeline_key.version == PIPELINE_VERSION_COLOR_PASS) {
		if (p_pipeline_key.color_pass_flags & PIPELINE_COLOR_PASS_FLAG_TRANSPARENT) {
			if (alpha_antialiasing_mode == ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE) {
				multisample_state.enable_alpha_to_coverage = true;
			} else if (alpha_antialiasing_mode == ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE_AND_TO_ONE) {
				multisample_state.enable_alpha_to_coverage = true;
				multisample_state.enable_alpha_to_one = true;
			}

			blend_state = blend_state_color_blend;

			if (depth_draw == DEPTH_DRAW_OPAQUE) {
				depth_stencil_state.enable_depth_write = false; //alpha does not draw depth
			}
		} else {
			blend_state = blend_state_color_opaque;

			if (depth_pre_pass_enabled) {
				// We already have a depth from the depth pre-pass, there is no need to write it again.
				// In addition we can use COMPARE_OP_EQUAL instead of COMPARE_OP_LESS_OR_EQUAL.
				// This way we can use the early depth test to discard transparent fragments before the fragment shader even starts.
				depth_stencil_state.depth_compare_operator = RD::COMPARE_OP_EQUAL;
				depth_stencil_state.enable_depth_write = false;
			}
		}
	} else {
		switch (p_pipeline_key.version) {
			case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS:
			case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_MULTIVIEW:
				blend_state = blend_state_depth_normal_roughness;
				break;
			case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI:
			case PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI_MULTIVIEW:
				blend_state = blend_state_depth_normal_roughness_giprobe;
				break;
			case PIPELINE_VERSION_DEPTH_PASS_WITH_MATERIAL:
				// Writes to normal and roughness in opaque way.
				blend_state = RD::PipelineColorBlendState::create_disabled(5);
				break;
			case PIPELINE_VERSION_DEPTH_PASS:
			case PIPELINE_VERSION_DEPTH_PASS_DP:
			case PIPELINE_VERSION_DEPTH_PASS_MULTIVIEW:
			case PIPELINE_VERSION_DEPTH_PASS_WITH_SDF:
			default:
				break;
		}
	}

	// Convert the specialization from the key to pipeline specialization constants.
	Vector<RD::PipelineSpecializationConstant> specialization_constants;
	RD::PipelineSpecializationConstant sc;
	sc.constant_id = 0;
	sc.int_value = p_pipeline_key.shader_specialization.packed_0;
	sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT;
	specialization_constants.push_back(sc);

	sc.constant_id = 1;
	sc.int_value = p_pipeline_key.shader_specialization.packed_1;
	sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT;
	specialization_constants.push_back(sc);

	sc = {}; // Sanitize value bits. "bool_value" only assigns 8 bits and keeps the remaining bits intact.
	sc.constant_id = 2;
	sc.bool_value = emulate_point_size_flag;
	sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL;
	specialization_constants.push_back(sc);

	RID shader_rid = get_shader_variant(p_pipeline_key.version, p_pipeline_key.color_pass_flags, p_pipeline_key.ubershader);
	ERR_FAIL_COND(shader_rid.is_null());

	uint64_t create_start = OS::get_singleton()->get_ticks_usec();
	RID pipeline = RD::get_singleton()->render_pipeline_create(shader_rid, p_pipeline_key.framebuffer_format_id, p_pipeline_key.vertex_format_id, primitive_rd, raster_state, multisample_state, depth_stencil_state, blend_state, 0, 0, specialization_constants);
	SceneShaderForwardClustered::singleton->pipelines_created[p_pipeline_key.ubershader ? 1 : 0].increment();
	SceneShaderForwardClustered::singleton->pipeline_create_usec[p_pipeline_key.ubershader ? 1 : 0].add(OS::get_singleton()->get_ticks_usec() - create_start);
	ERR_FAIL_COND(pipeline.is_null());

	pipeline_hash_map.add_compiled_pipeline(p_pipeline_key, pipeline);
}

RD::PolygonCullMode SceneShaderForwardClustered::ShaderData::get_cull_mode_from_cull_variant(CullVariant p_cull_variant) {
	const RD::PolygonCullMode cull_mode_rd_table[CULL_VARIANT_MAX][3] = {
		{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_FRONT, RD::POLYGON_CULL_BACK },
		{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_BACK, RD::POLYGON_CULL_FRONT },
		{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_DISABLED }
	};

	return cull_mode_rd_table[p_cull_variant][cull_mode];
}

RID SceneShaderForwardClustered::ShaderData::_get_shader_variant(uint16_t p_shader_version) const {
	if (version.is_valid()) {
		ERR_FAIL_NULL_V(SceneShaderForwardClustered::singleton, RID());
		SceneShaderForwardClustered::singleton->_ensure_variant_group_enabled(p_shader_version);
		return SceneShaderForwardClustered::singleton->shader.version_get_shader(version, p_shader_version);
	} else {
		return RID();
	}
}

void SceneShaderForwardClustered::ShaderData::_clear_vertex_input_mask_cache() {
	for (uint32_t i = 0; i < VERTEX_INPUT_MASKS_SIZE; i++) {
		vertex_input_masks[i].store(0);
	}
}

RID SceneShaderForwardClustered::ShaderData::get_shader_variant(PipelineVersion p_pipeline_version, uint32_t p_color_pass_flags, bool p_ubershader) const {
	return _get_shader_variant(_get_shader_version(p_pipeline_version, p_color_pass_flags, p_ubershader));
}

uint64_t SceneShaderForwardClustered::ShaderData::get_vertex_input_mask(PipelineVersion p_pipeline_version, uint32_t p_color_pass_flags, bool p_ubershader) {
	// Vertex input masks require knowledge of the shader. Since querying the shader can be expensive due to high contention and the necessary mutex, we cache the result instead.
	uint16_t shader_version = _get_shader_version(p_pipeline_version, p_color_pass_flags, p_ubershader);
	uint64_t input_mask = vertex_input_masks[shader_version].load(std::memory_order_relaxed);
	if (input_mask == 0) {
		RID shader_rid = _get_shader_variant(shader_version);
		ERR_FAIL_COND_V(shader_rid.is_null(), 0);

		input_mask = RD::get_singleton()->shader_get_vertex_input_attribute_mask(shader_rid);
		vertex_input_masks[shader_version].store(input_mask, std::memory_order_relaxed);
	}

	return input_mask;
}

bool SceneShaderForwardClustered::ShaderData::is_valid() const {
	if (version.is_valid()) {
		ERR_FAIL_NULL_V(SceneShaderForwardClustered::singleton, false);
		return SceneShaderForwardClustered::singleton->shader.version_is_valid(version);
	} else {
		return false;
	}
}

SceneShaderForwardClustered::ShaderData::ShaderData() :
		shader_list_element(this) {
	pipeline_hash_map.set_creation_object_and_function(this, &ShaderData::_create_pipeline);
	pipeline_hash_map.set_compilations(SceneShaderForwardClustered::singleton->pipeline_compilations, &SceneShaderForwardClustered::singleton_mutex);
}

SceneShaderForwardClustered::ShaderData::~ShaderData() {
	pipeline_hash_map.clear_pipelines();
	{
		MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
		MaterialTable::unref(material_table);
		material_table = nullptr;
	}

	if (version.is_valid()) {
		ERR_FAIL_NULL(SceneShaderForwardClustered::singleton);
		SceneShaderForwardClustered::singleton->shader.version_free(version);
	}

	if (rt) {
		memdelete(rt);
		rt = nullptr;
	}
}

RendererRD::MaterialStorage::ShaderData *SceneShaderForwardClustered::_create_shader_func() {
	MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
	ShaderData *shader_data = memnew(ShaderData);
	singleton->shader_list.add(&shader_data->shader_list_element);
	return shader_data;
}

void SceneShaderForwardClustered::MaterialData::set_render_priority(int p_priority) {
	priority = p_priority - RSE::MATERIAL_RENDER_PRIORITY_MIN; //8 bits
}

void SceneShaderForwardClustered::MaterialData::set_next_pass(RID p_pass) {
	next_pass = p_pass;
}

/* Material table (D036) */

uint32_t SceneShaderForwardClustered::MaterialTable::GroupKey::hash() const {
	uint32_t h = hash_murmur3_one_64(material_feedback.get_id(), hash_murmur3_one_32(page));
	for (const RID &texture : textures) {
		h = hash_murmur3_one_64(texture.get_id(), h);
	}
	return hash_fmix32(h);
}

void SceneShaderForwardClustered::MaterialTable::allocate_slot(uint32_t &r_page, uint32_t &r_slot) {
	// Fill the first pages first, so that materials share as few groups as possible.
	for (uint32_t i = 0; i < pages.size(); i++) {
		Page &page = pages[i];
		if (!page.free_slots.is_empty()) {
			r_page = i;
			r_slot = page.free_slots[page.free_slots.size() - 1];
			page.free_slots.resize(page.free_slots.size() - 1);
			return;
		}
		if (page.used < page.capacity) {
			r_page = i;
			r_slot = page.used++;
			return;
		}
	}
	// Pages grow (16, 32, ... 4096 slots) and never move: their uniform sets stay valid.
	Page page;
	page.capacity = pages.is_empty() ? FIRST_PAGE_SLOTS : MIN(pages[pages.size() - 1].capacity * 2, MAX_PAGE_SLOTS);
	page.buffer = RD::get_singleton()->storage_buffer_create(page.capacity * stride);
	page.used = 1;
	r_page = pages.size();
	r_slot = 0;
	pages.push_back(page);
}

void SceneShaderForwardClustered::MaterialTable::free_slot(uint32_t p_page, uint32_t p_slot) {
	ERR_FAIL_UNSIGNED_INDEX(p_page, pages.size());
	pages[p_page].free_slots.push_back(p_slot);
}

void SceneShaderForwardClustered::MaterialTable::unref(MaterialTable *p_table) {
	if (!p_table) {
		return;
	}
	ERR_FAIL_COND(p_table->refcount == 0);
	p_table->refcount--;
	if (p_table->refcount > 0) {
		return;
	}
	// Every material left already, so no groups remain.
	DEV_ASSERT(p_table->groups.is_empty());
	for (Page &page : p_table->pages) {
		if (page.buffer.is_valid()) {
			RD::get_singleton()->free_rid(page.buffer);
		}
	}
	memdelete(p_table);
}

void SceneShaderForwardClustered::MaterialTable::_group_uniform_set_erased(void *p_group) {
	// A texture of the group was freed (or the shader changed): each member
	// finds or makes a new uniform set at its next update.
	Group *group = static_cast<Group *>(p_group);
	group->uniform_set = RID();
	for (MaterialData *member : group->members) {
		member->notify_uniform_set_erased();
	}
}

void SceneShaderForwardClustered::MaterialData::_table_leave_group() {
	if (!group) {
		return;
	}
	MaterialTable::Group *g = group;
	group = nullptr;
	uniform_set = RID();
	MaterialData *last = g->members[g->members.size() - 1];
	g->members[group_member] = last;
	last->group_member = group_member;
	g->members.resize(g->members.size() - 1);
	if (!g->members.is_empty()) {
		return;
	}
	if (g->uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(g->uniform_set)) {
		RD::get_singleton()->uniform_set_set_invalidation_callback(g->uniform_set, nullptr, nullptr);
		RD::get_singleton()->free_rid(g->uniform_set);
	}
	table->groups.erase(g->key);
	memdelete(g);
}

void SceneShaderForwardClustered::MaterialData::_table_release() {
	if (!table) {
		return;
	}
	_table_leave_group();
	if (table_page != UINT32_MAX) {
		table->free_slot(table_page, table_slot);
		table_page = UINT32_MAX;
		table_slot = 0;
	}
	MaterialTable::unref(table);
	table = nullptr;
}

bool SceneShaderForwardClustered::MaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	if (!shader_data->version.is_valid()) {
		return false;
	}
	// The material set's layout is the same in every variant: take it from the
	// color ubershader, which every shader builds (its depth variants may be skipped).
	RID shader_rid = SceneShaderForwardClustered::singleton->shader.version_get_shader(shader_data->version, ShaderVersion::SHADER_VERSION_COLOR_PASS * 2 + SHADER_COLOR_PASS_FLAG_UBERSHADER);

	MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
	RD *rd = RD::get_singleton();
	// True when the uniform set or the slot changed: surface caches hold both.
	bool changed = false;
	if (table != shader_data->material_table) {
		// New shader code with another parameter size.
		_table_release();
		changed = true;
		if (!shader_data->material_table) {
			return changed; // No code compiled yet.
		}
		table = shader_data->material_table;
		table->refcount++;
		p_uniform_dirty = true;
		p_textures_dirty = true;
	}
	if (!table) {
		return changed;
	}

	const uint32_t stride = table->stride;
	if (stride > 0 && table_page == UINT32_MAX) {
		table->allocate_slot(table_page, table_slot);
		p_uniform_dirty = true;
		p_textures_dirty = true; // The group depends on the page.
		changed = true;
	}
	if (uint32_t(table_ubo.size()) != stride) {
		table_ubo.resize(stride);
		if (stride > 0) {
			memset(table_ubo.ptrw(), 0, stride);
		}
		p_uniform_dirty = true;
	}
	if (p_uniform_dirty && stride > 0) {
		update_uniform_buffer(shader_data->uniforms, shader_data->ubo_offsets.ptr(), p_parameters, table_ubo.ptrw(), stride, true);
		rd->buffer_update(table->pages[table_page].buffer, table_slot * stride, stride, table_ubo.ptr());
	}

	uint32_t texture_count = 0;
	for (const ShaderCompiler::GeneratedCode::Texture &texture : shader_data->texture_uniforms) {
		texture_count += uint32_t(texture.array_size > 0 ? texture.array_size : 1);
	}
	if (uint32_t(table_textures.size()) != texture_count || p_textures_dirty) {
		table_textures.resize(texture_count);
		render_target_cache.clear();
		p_textures_dirty = true;
	}
	if (p_textures_dirty && texture_count > 0) {
		update_textures(p_parameters, shader_data->default_texture_params, shader_data->texture_uniforms, table_textures.ptrw(), true, true);
	}

	if (stride == 0 && shader_data->texture_uniforms.is_empty()) {
		// No uniform set needed.
		if (group) {
			_table_leave_group();
			changed = true;
		}
		return changed;
	}

	if (!p_textures_dirty && group && group->uniform_set.is_valid() && rd->uniform_set_is_valid(group->uniform_set)) {
		return changed; // Only the parameters changed, in place.
	}

	MaterialTable::GroupKey key;
	key.page = table_page == UINT32_MAX ? 0 : table_page;
	key.textures = table_textures;
	key.material_feedback = material_feedback_rid;
	MaterialTable::Group *found = nullptr;
	if (MaterialTable::Group **existing = table->groups.getptr(key)) {
		found = *existing;
	}
	if (!found || found != group) {
		_table_leave_group();
		if (!found) {
			found = memnew(MaterialTable::Group);
			found->id = ++SceneShaderForwardClustered::singleton->material_group_last_id;
			found->key = key;
			table->groups.insert(key, found);
		}
		group = found;
		group_member = found->members.size();
		found->members.push_back(this);
		changed = true;
	}
	if (group->uniform_set.is_null() || !rd->uniform_set_is_valid(group->uniform_set)) {
		Vector<RD::Uniform> uniforms;
		if (stride > 0) {
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = 0;
			u.append_id(table->pages[table_page].buffer);
			uniforms.push_back(u);
		}
		const RID *textures = table_textures.ptr();
		for (int i = 0, k = 0; i < shader_data->texture_uniforms.size(); i++) {
			const int array_size = shader_data->texture_uniforms[i].array_size;
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			u.binding = 1 + k;
			if (array_size > 0) {
				for (int j = 0; j < array_size; j++) {
					u.append_id(textures[k++]);
				}
			} else {
				u.append_id(textures[k++]);
			}
			uniforms.push_back(u);
		}
		group->uniform_set = rd->uniform_set_create(uniforms, shader_rid, RenderForwardClustered::MATERIAL_UNIFORM_SET);
		rd->uniform_set_set_invalidation_callback(group->uniform_set, MaterialTable::_group_uniform_set_erased, group);
	}
	if (uniform_set != group->uniform_set) {
		uniform_set = group->uniform_set;
		changed = true;
	}
	return changed;
}

SceneShaderForwardClustered::MaterialData::~MaterialData() {
	MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
	_table_release();
}

RendererRD::MaterialStorage::MaterialData *SceneShaderForwardClustered::_create_material_func(ShaderData *p_shader) {
	MaterialData *material_data = memnew(MaterialData);
	material_data->shader_data = p_shader;
	//update will happen later anyway so do nothing.
	return material_data;
}

SceneShaderForwardClustered *SceneShaderForwardClustered::singleton = nullptr;
Mutex SceneShaderForwardClustered::singleton_mutex;

SceneShaderForwardClustered::SceneShaderForwardClustered() {
	// there should be only one of these, contained within our RenderFM singleton.
	singleton = this;
}

SceneShaderForwardClustered::~SceneShaderForwardClustered() {
	if (OS::get_singleton()->is_stdout_verbose()) {
		print_line(vformat("FORWARD_PIPELINE_STATS: specialized=%d specialized_ms=%.1f ubershader=%d ubershader_ms=%.1f",
				pipelines_created[0].get(), pipeline_create_usec[0].get() / 1000.0, pipelines_created[1].get(), pipeline_create_usec[1].get() / 1000.0));
	}

	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();

	RD::get_singleton()->free_rid(default_vec4_xform_buffer);
	RD::get_singleton()->free_rid(shadow_sampler);
	RD::get_singleton()->free_rid(default_material_feedback_buffer);

	material_storage->shader_free(overdraw_material_shader);
	material_storage->shader_free(default_shader);
	material_storage->shader_free(debug_shadow_splits_material_shader);
	material_storage->shader_free(debug_cluster_lod_material_shader);

	material_storage->material_free(overdraw_material);
	material_storage->material_free(default_material);
	material_storage->material_free(debug_shadow_splits_material);
	material_storage->material_free(debug_cluster_lod_material);
}

void SceneShaderForwardClustered::init(const String p_defines) {
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();

	emulate_point_size = !RD::get_singleton()->has_feature(RD::SUPPORTS_POINT_SIZE);

	depth_prepass_enabled = GLOBAL_GET("rendering/driver/depth_prepass/enable");

	{
		Vector<ShaderRD::VariantDefine> shader_versions;
		auto add_version = [&](uint32_t p_features, const String &p_defines) {
			uint32_t group = _get_variant_group(p_features);
			shader_versions.push_back(ShaderRD::VariantDefine(group, p_defines, p_features == 0));
		};
		for (uint32_t ubershader = 0; ubershader < 2; ubershader++) {
			const String base_define = ubershader ? "\n#define UBERSHADER\n" : "";
			add_version(0, base_define + "\n#define MODE_RENDER_DEPTH\n"); // SHADER_VERSION_DEPTH_PASS
			add_version(SHADER_FEATURE_DUAL_PARABOLOID, base_define + "\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n"); // SHADER_VERSION_DEPTH_PASS_DP
			add_version(SHADER_FEATURE_NORMAL_ROUGHNESS, base_define + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n"); // SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS
			add_version(SHADER_FEATURE_VOXEL_GI, base_define + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n#define MODE_RENDER_VOXEL_GI\n"); // SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI
			add_version(SHADER_FEATURE_MULTIVIEW, base_define + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n"); // SHADER_VERSION_DEPTH_PASS_MULTIVIEW
			add_version(SHADER_FEATURE_MULTIVIEW | SHADER_FEATURE_NORMAL_ROUGHNESS, base_define + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n"); // SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_MULTIVIEW
			add_version(SHADER_FEATURE_MULTIVIEW | SHADER_FEATURE_VOXEL_GI, base_define + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n#define MODE_RENDER_VOXEL_GI\n"); // SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI_MULTIVIEW
			add_version(SHADER_FEATURE_MATERIAL, base_define + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_MATERIAL\n"); // SHADER_VERSION_DEPTH_PASS_WITH_MATERIAL
			add_version(SHADER_FEATURE_SDF, base_define + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_SDF\n"); // SHADER_VERSION_DEPTH_PASS_WITH_SDF
		}

		Vector<String> color_pass_flags = {
			"\n#define UBERSHADER\n", // SHADER_COLOR_PASS_FLAG_UBERSHADER
			"\n#define MODE_SEPARATE_SPECULAR\n", // SHADER_COLOR_PASS_FLAG_SEPARATE_SPECULAR
			"\n#define USE_LIGHTMAP\n", // SHADER_COLOR_PASS_FLAG_LIGHTMAP
			"\n#define USE_MULTIVIEW\n", // SHADER_COLOR_PASS_FLAG_MULTIVIEW
			"\n#define MOTION_VECTORS\n", // SHADER_COLOR_PASS_FLAG_MOTION_VECTORS
		};

		for (int i = 0; i < SHADER_COLOR_PASS_FLAG_COUNT; i++) {
			String version = "";
			for (int j = 0; (1 << j) < SHADER_COLOR_PASS_FLAG_COUNT; j += 1) {
				if ((1 << j) & i) {
					version += color_pass_flags[j];
				}
			}

			// Group by the features this pass needs; the ubershader flag needs none.
			uint32_t features = 0;
			features |= (i & SHADER_COLOR_PASS_FLAG_SEPARATE_SPECULAR) ? SHADER_FEATURE_SEPARATE_SPECULAR : 0;
			features |= (i & SHADER_COLOR_PASS_FLAG_LIGHTMAP) ? SHADER_FEATURE_LIGHTMAP : 0;
			features |= (i & SHADER_COLOR_PASS_FLAG_MULTIVIEW) ? SHADER_FEATURE_MULTIVIEW : 0;
			features |= (i & SHADER_COLOR_PASS_FLAG_MOTION_VECTORS) ? SHADER_FEATURE_MOTION_VECTORS : 0;
			add_version(features, version);
		}

		Vector<uint64_t> dynamic_buffers;
		dynamic_buffers.push_back(ShaderRD::DynamicBuffer::encode(RenderForwardClustered::RENDER_PASS_UNIFORM_SET, 2));
		shader.initialize(shader_versions, p_defines, Vector<RD::PipelineImmutableSampler>(), dynamic_buffers);

		if (RendererCompositorRD::get_singleton()->is_xr_enabled()) {
			enable_shader_features(SHADER_FEATURE_MULTIVIEW);
		}
	}

	material_storage->shader_set_data_request_function(RendererRD::MaterialStorage::SHADER_TYPE_3D, _create_shader_funcs);
	material_storage->material_set_data_request_function(RendererRD::MaterialStorage::SHADER_TYPE_3D, _create_material_funcs);

	{
		//shader compiler
		ShaderCompiler::DefaultIdentifierActions actions;

		actions.renames["MODEL_MATRIX"] = "read_model_matrix";
		actions.renames["MODEL_NORMAL_MATRIX"] = "model_normal_matrix";
		actions.renames["VIEW_MATRIX"] = "read_view_matrix";
		actions.renames["INV_VIEW_MATRIX"] = "inv_view_matrix";
		actions.renames["PROJECTION_MATRIX"] = "projection_matrix";
		actions.renames["INV_PROJECTION_MATRIX"] = "inv_projection_matrix";
		actions.renames["MODELVIEW_MATRIX"] = "modelview";
		actions.renames["MODELVIEW_NORMAL_MATRIX"] = "modelview_normal";
		actions.renames["MAIN_CAM_INV_VIEW_MATRIX"] = "scene_data.main_cam_inv_view_matrix";

		actions.renames["VERTEX"] = "vertex";
		actions.renames["NORMAL"] = "normal_highp";
		actions.renames["TANGENT"] = "tangent";
		actions.renames["BINORMAL"] = "binormal";
		actions.renames["POSITION"] = "position";
		actions.renames["UV"] = "uv_interp";
		actions.renames["STREAMING_LOD"] = "streaming_lod";
		actions.renames["UV2"] = "uv2_interp";
		actions.renames["COLOR"] = "color_interp";
		actions.renames["POINT_SIZE"] = "point_size";
		actions.renames["INSTANCE_ID"] = "INSTANCE_INDEX";
		actions.renames["VERTEX_ID"] = "VERTEX_INDEX";
		actions.renames["Z_CLIP_SCALE"] = "z_clip_scale";

		actions.renames["ALPHA_SCISSOR_THRESHOLD"] = "alpha_scissor_threshold";
		actions.renames["ALPHA_HASH_SCALE"] = "alpha_hash_scale";
		actions.renames["ALPHA_ANTIALIASING_EDGE"] = "alpha_antialiasing_edge";
		actions.renames["ALPHA_TEXTURE_COORDINATE"] = "alpha_texture_coordinate";

		//builtins

		actions.renames["TIME"] = "global_time";
		actions.renames["PREV_TIME"] = "global_prev_time";
		actions.renames["EXPOSURE"] = "(1.0 / scene_data_block.data.emissive_exposure_normalization)";
		actions.renames["PI"] = String::num(Math::PI);
		actions.renames["TAU"] = String::num(Math::TAU);
		actions.renames["E"] = String::num(Math::E);
		actions.renames["OUTPUT_IS_SRGB"] = "SHADER_IS_SRGB";
		actions.renames["CLIP_SPACE_FAR"] = "SHADER_SPACE_FAR";
		actions.renames["IN_SHADOW_PASS"] = "bool(scene_data_block.data.flags & SCENE_DATA_FLAGS_IN_SHADOW_PASS)";
		actions.renames["VIEWPORT_SIZE"] = "read_viewport_size";

		actions.renames["FRAGCOORD"] = "gl_FragCoord";
		actions.renames["FRONT_FACING"] = "gl_FrontFacing";
		actions.renames["NORMAL_MAP"] = "normal_map";
		actions.renames["NORMAL_MAP_DEPTH"] = "normal_map_depth";
		actions.renames["BENT_NORMAL_MAP"] = "bent_normal_map";
		actions.renames["ALBEDO"] = "albedo_highp";
		actions.renames["ALPHA"] = "alpha_highp";
		actions.renames["PREMUL_ALPHA_FACTOR"] = "premul_alpha";
		actions.renames["METALLIC"] = "metallic_highp";
		actions.renames["SPECULAR"] = "specular";
		actions.renames["ROUGHNESS"] = "roughness_highp";
		actions.renames["RIM"] = "rim";
		actions.renames["RIM_TINT"] = "rim_tint";
		actions.renames["CLEARCOAT"] = "clearcoat";
		actions.renames["CLEARCOAT_ROUGHNESS"] = "clearcoat_roughness";
		actions.renames["ANISOTROPY"] = "anisotropy";
		actions.renames["ANISOTROPY_FLOW"] = "anisotropy_flow";
		actions.renames["SSS_STRENGTH"] = "sss_strength";
		actions.renames["SSS_TRANSMITTANCE_COLOR"] = "transmittance_color";
		actions.renames["SSS_TRANSMITTANCE_DEPTH"] = "transmittance_depth";
		actions.renames["SSS_TRANSMITTANCE_BOOST"] = "transmittance_boost";
		actions.renames["BACKLIGHT"] = "backlight";
		actions.renames["AO"] = "ao";
		actions.renames["AO_LIGHT_AFFECT"] = "ao_light_affect";
		actions.renames["EMISSION"] = "emission";
		actions.renames["POINT_COORD"] = "point_coord";
		actions.renames["INSTANCE_CUSTOM"] = "instance_custom";
		actions.renames["SCREEN_UV"] = "screen_uv";
		actions.renames["DEPTH"] = "gl_FragDepth";
		actions.renames["FOG"] = "fog";
		actions.renames["RADIANCE"] = "custom_radiance";
		actions.renames["IRRADIANCE"] = "custom_irradiance";
		actions.renames["BONE_INDICES"] = "bone_attrib";
		actions.renames["BONE_WEIGHTS"] = "weight_attrib";
		actions.renames["CUSTOM0"] = "custom0_attrib";
		actions.renames["CUSTOM1"] = "custom1_attrib";
		actions.renames["CUSTOM2"] = "custom2_attrib";
		actions.renames["CUSTOM3"] = "custom3_attrib";
		actions.renames["LIGHT_VERTEX"] = "light_vertex";

		actions.renames["NODE_POSITION_WORLD"] = "read_model_matrix[3].xyz";
		actions.renames["CAMERA_POSITION_WORLD"] = "inv_view_matrix[3].xyz";
		actions.renames["CAMERA_DIRECTION_WORLD"] = "inv_view_matrix[2].xyz";
		actions.renames["CAMERA_VISIBLE_LAYERS"] = "scene_data.camera_visible_layers";
		actions.renames["NODE_POSITION_VIEW"] = "(read_view_matrix * read_model_matrix)[3].xyz";

		actions.renames["IS_MULTIVIEW"] = "OUTPUT_IS_MULTIVIEW";
		actions.renames["VIEW_INDEX"] = "ViewIndex";
		actions.renames["VIEW_MONO_LEFT"] = "0";
		actions.renames["VIEW_RIGHT"] = "1";
		actions.renames["EYE_OFFSET"] = "eye_offset";

		//for light
		actions.renames["VIEW"] = "view_highp";
		actions.renames["SPECULAR_AMOUNT"] = "specular_amount_highp";
		actions.renames["LIGHT_COLOR"] = "light_color_highp";
		actions.renames["LIGHT_IS_DIRECTIONAL"] = "is_directional";
		actions.renames["LIGHT_IS_AREA"] = "is_area";
		actions.renames["LIGHT_AREA_DIFFUSE_MULTIPLIER"] = "area_diffuse";
		actions.renames["LIGHT_AREA_SPECULAR_MULTIPLIER"] = "area_specular";
		actions.renames["LIGHT"] = "light_highp";
		actions.renames["ATTENUATION"] = "attenuation_highp";
		actions.renames["DIFFUSE_LIGHT"] = "diffuse_light_highp";
		actions.renames["SPECULAR_LIGHT"] = "specular_light_highp";

		actions.usage_defines["NORMAL"] = "#define NORMAL_USED\n";
		actions.usage_defines["TANGENT"] = "#define TANGENT_USED\n";
		actions.usage_defines["BINORMAL"] = "@TANGENT";
		actions.usage_defines["RIM"] = "#define LIGHT_RIM_USED\n";
		actions.usage_defines["RIM_TINT"] = "@RIM";
		actions.usage_defines["CLEARCOAT"] = "#define LIGHT_CLEARCOAT_USED\n";
		actions.usage_defines["CLEARCOAT_ROUGHNESS"] = "@CLEARCOAT";
		actions.usage_defines["ANISOTROPY"] = "#define LIGHT_ANISOTROPY_USED\n";
		actions.usage_defines["ANISOTROPY_FLOW"] = "@ANISOTROPY";
		actions.usage_defines["AO"] = "#define AO_USED\n";
		actions.usage_defines["AO_LIGHT_AFFECT"] = "#define AO_USED\n";
		actions.usage_defines["UV"] = "#define UV_USED\n";
		actions.usage_defines["UV2"] = "#define UV2_USED\n";
		actions.usage_defines["BONE_INDICES"] = "#define BONES_USED\n";
		actions.usage_defines["BONE_WEIGHTS"] = "#define WEIGHTS_USED\n";
		actions.usage_defines["CUSTOM0"] = "#define CUSTOM0_USED\n";
		actions.usage_defines["CUSTOM1"] = "#define CUSTOM1_USED\n";
		actions.usage_defines["CUSTOM2"] = "#define CUSTOM2_USED\n";
		actions.usage_defines["CUSTOM3"] = "#define CUSTOM3_USED\n";
		actions.usage_defines["NORMAL_MAP"] = "#define NORMAL_MAP_USED\n";
		actions.usage_defines["NORMAL_MAP_DEPTH"] = "@NORMAL_MAP";
		actions.usage_defines["BENT_NORMAL_MAP"] = "#define BENT_NORMAL_MAP_USED\n";
		actions.usage_defines["COLOR"] = "#define COLOR_USED\n";
		actions.usage_defines["INSTANCE_CUSTOM"] = "#define ENABLE_INSTANCE_CUSTOM\n";
		actions.usage_defines["POSITION"] = "#define OVERRIDE_POSITION\n";
		actions.usage_defines["LIGHT_VERTEX"] = "#define LIGHT_VERTEX_USED\n";
		actions.usage_defines["Z_CLIP_SCALE"] = "#define Z_CLIP_SCALE_USED\n";
		actions.usage_defines["LIGHT_AREA_DIFFUSE_MULTIPLIER"] = "#define AREA_LIGHT_CODE_USED\n";
		actions.usage_defines["LIGHT_AREA_SPECULAR_MULTIPLIER"] = "@LIGHT_AREA_DIFFUSE_MULTIPLIER";
		actions.usage_defines["LIGHT_IS_AREA"] = "@LIGHT_AREA_DIFFUSE_MULTIPLIER";

		actions.usage_defines["ALPHA_SCISSOR_THRESHOLD"] = "#define ALPHA_SCISSOR_USED\n";
		actions.usage_defines["ALPHA_HASH_SCALE"] = "#define ALPHA_HASH_USED\n";
		actions.usage_defines["ALPHA_ANTIALIASING_EDGE"] = "#define ALPHA_ANTIALIASING_EDGE_USED\n";
		actions.usage_defines["ALPHA_TEXTURE_COORDINATE"] = "@ALPHA_ANTIALIASING_EDGE";
		actions.usage_defines["PREMUL_ALPHA_FACTOR"] = "#define PREMUL_ALPHA_USED\n";

		actions.usage_defines["SSS_STRENGTH"] = "#define ENABLE_SSS\n";
		actions.usage_defines["SSS_TRANSMITTANCE_DEPTH"] = "#define ENABLE_TRANSMITTANCE\n";
		actions.usage_defines["BACKLIGHT"] = "#define LIGHT_BACKLIGHT_USED\n";
		actions.usage_defines["SCREEN_UV"] = "#define SCREEN_UV_USED\n";

		actions.usage_defines["FOG"] = "#define CUSTOM_FOG_USED\n";
		actions.usage_defines["RADIANCE"] = "#define CUSTOM_RADIANCE_USED\n";
		actions.usage_defines["IRRADIANCE"] = "#define CUSTOM_IRRADIANCE_USED\n";

		actions.usage_defines["MODEL_MATRIX"] = "#define MODEL_MATRIX_USED\n";

		actions.usage_defines["POINT_SIZE"] = "#define POINT_SIZE_USED\n";
		actions.usage_defines["POINT_COORD"] = "#define POINT_COORD_USED\n";

		actions.usage_defines["DISCARD"] = "#define DISCARD_USED\n";
		actions.usage_defines["DEPTH"] = "#define DEPTH_USED\n";

		actions.usage_defines["STREAMING_LOD"] = "#define STREAMING_LOD_USED\n";

		actions.render_mode_defines["skip_vertex_transform"] = "#define SKIP_TRANSFORM_USED\n";
		actions.render_mode_defines["world_vertex_coords"] = "#define VERTEX_WORLD_COORDS_USED\n";
		actions.render_mode_defines["ensure_correct_normals"] = "#define ENSURE_CORRECT_NORMALS\n";
		actions.render_mode_defines["cull_front"] = "#define DO_SIDE_CHECK\n";
		actions.render_mode_defines["cull_disabled"] = "#define DO_SIDE_CHECK\n";
		actions.render_mode_defines["particle_trails"] = "#define USE_PARTICLE_TRAILS\n";
		actions.render_mode_defines["depth_prepass_alpha"] = "#define USE_OPAQUE_PREPASS\n";

		actions.render_mode_defines["depth_draw_never"] = "#define DEPTH_DRAW_NEVER_USED\n";
		actions.render_mode_defines["depth_draw_always"] = "#define DEPTH_DRAW_ALWAYS_USED\n";
		actions.render_mode_defines["depth_test_disabled"] = "#define DEPTH_TEST_DISABLED_USED\n";
		actions.stencil_mode_defines["write"] = "#define STENCIL_WRITE_USED\n";
		actions.stencil_mode_defines["write_depth_fail"] = "#define STENCIL_WRITE_USED\n";

		bool force_lambert = GLOBAL_GET("rendering/shading/overrides/force_lambert_over_burley");

		if (!force_lambert) {
			actions.render_mode_defines["diffuse_burley"] = "#define DIFFUSE_BURLEY\n";
		}

		actions.render_mode_defines["diffuse_lambert_wrap"] = "#define DIFFUSE_LAMBERT_WRAP\n";
		actions.render_mode_defines["diffuse_toon"] = "#define DIFFUSE_TOON\n";

		actions.render_mode_defines["sss_mode_skin"] = "#define SSS_MODE_SKIN\n";

		actions.render_mode_defines["specular_schlick_ggx"] = "#define SPECULAR_SCHLICK_GGX\n";

		actions.render_mode_defines["specular_toon"] = "#define SPECULAR_TOON\n";
		actions.render_mode_defines["specular_disabled"] = "#define SPECULAR_DISABLED\n";
		actions.render_mode_defines["shadows_disabled"] = "#define SHADOWS_DISABLED\n";
		actions.render_mode_defines["ambient_light_disabled"] = "#define AMBIENT_LIGHT_DISABLED\n";
		actions.render_mode_defines["shadow_to_opacity"] = "#define USE_SHADOW_TO_OPACITY\n";
		actions.render_mode_defines["unshaded"] = "#define MODE_UNSHADED\n";

		bool force_vertex_shading = GLOBAL_GET("rendering/shading/overrides/force_vertex_shading");
		if (!force_vertex_shading) {
			// If forcing vertex shading, this will be defined already.
			actions.render_mode_defines["vertex_lighting"] = "#define USE_VERTEX_LIGHTING\n";
		}

		actions.render_mode_defines["debug_shadow_splits"] = "#define DEBUG_DRAW_PSSM_SPLITS\n";
		actions.render_mode_defines["debug_cluster_lod"] = "#define DEBUG_DRAW_CLUSTER_LOD\n";
		actions.render_mode_defines["fog_disabled"] = "#define FOG_DISABLED\n";

		actions.render_mode_defines["specular_occlusion_disabled"] = "#define SPECULAR_OCCLUSION_DISABLED\n";

		actions.base_texture_binding_index = 1;
		actions.texture_layout_set = RenderForwardClustered::MATERIAL_UNIFORM_SET;
		actions.base_uniform_string = "material.";
		actions.base_varying_index = 15;

		actions.default_filter = ShaderLanguage::FILTER_LINEAR_MIPMAP;
		actions.default_repeat = ShaderLanguage::REPEAT_ENABLE;
		actions.global_buffer_array_variable = "global_shader_uniforms.data";
		actions.instance_uniform_index_variable = "instances.data[instance_index_interp].instance_uniforms_ofs";

		actions.check_multiview_samplers = true;

		compiler.initialize(actions);
	}

	{
		//default material and shader
		default_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(default_shader);
		initializing_default_shader = true;
		material_storage->shader_set_code(default_shader, R"(
// Default 3D material shader (Forward+).

shader_type spatial;

void vertex() {
	ROUGHNESS = 0.8;
}

void fragment() {
	ALBEDO = vec3(0.6);
	ROUGHNESS = 0.8;
	METALLIC = 0.2;
}
)");
		initializing_default_shader = false;
		default_material = material_storage->material_allocate();
		material_storage->material_initialize(default_material);
		material_storage->material_set_shader(default_material, default_shader);

		MaterialData *md = static_cast<MaterialData *>(material_storage->material_get_data(default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		default_shader_rd = md->shader_data->get_shader_variant(PIPELINE_VERSION_COLOR_PASS, 0, false);

		default_material_shader_ptr = md->shader_data;
		default_material_uniform_set = md->uniform_set;
	}

	{
		overdraw_material_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(overdraw_material_shader);
		// Use relatively low opacity so that more "layers" of overlapping objects can be distinguished.
		material_storage->shader_set_code(overdraw_material_shader, R"(
// 3D editor Overdraw debug draw mode shader (Forward+).

shader_type spatial;

render_mode blend_add, unshaded, fog_disabled;

void fragment() {
	ALBEDO = vec3(0.4, 0.8, 0.8);
	ALPHA = 0.1;
}
)");
		overdraw_material = material_storage->material_allocate();
		material_storage->material_initialize(overdraw_material);
		material_storage->material_set_shader(overdraw_material, overdraw_material_shader);

		MaterialData *md = static_cast<MaterialData *>(material_storage->material_get_data(overdraw_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		overdraw_material_shader_ptr = md->shader_data;
		overdraw_material_uniform_set = md->uniform_set;
	}

	{
		debug_shadow_splits_material_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(debug_shadow_splits_material_shader);
		material_storage->shader_set_code(debug_shadow_splits_material_shader, R"(
// 3D debug shadow splits mode shader (Forward+).

shader_type spatial;

render_mode debug_shadow_splits, fog_disabled;

void fragment() {
	ALBEDO = vec3(1.0, 1.0, 1.0);
}
)");
		debug_shadow_splits_material = material_storage->material_allocate();
		material_storage->material_initialize(debug_shadow_splits_material);
		material_storage->material_set_shader(debug_shadow_splits_material, debug_shadow_splits_material_shader);

		MaterialData *md = static_cast<MaterialData *>(material_storage->material_get_data(debug_shadow_splits_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		debug_shadow_splits_material_shader_ptr = md->shader_data;
		debug_shadow_splits_material_uniform_set = md->uniform_set;
	}

	{
		debug_cluster_lod_material_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(debug_cluster_lod_material_shader);
		material_storage->shader_set_code(debug_cluster_lod_material_shader, R"(
// 3D editor Cluster LOD debug draw mode shader (Forward+).
// The scene shader colors each cluster group (DEBUG_DRAW_CLUSTER_LOD).

shader_type spatial;

render_mode debug_cluster_lod, unshaded, fog_disabled;

void fragment() {
	ALBEDO = vec3(1.0);
}
)");
		debug_cluster_lod_material = material_storage->material_allocate();
		material_storage->material_initialize(debug_cluster_lod_material);
		material_storage->material_set_shader(debug_cluster_lod_material, debug_cluster_lod_material_shader);

		MaterialData *md = static_cast<MaterialData *>(material_storage->material_get_data(debug_cluster_lod_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		debug_cluster_lod_material_shader_ptr = md->shader_data;
		debug_cluster_lod_material_uniform_set = md->uniform_set;
	}

	{
		default_vec4_xform_buffer = RD::get_singleton()->storage_buffer_create(256);
		default_material_feedback_buffer = RD::get_singleton()->storage_buffer_create(256);
		Vector<RD::Uniform> uniforms;
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		u.append_id(default_vec4_xform_buffer);
		u.binding = 0;
		uniforms.push_back(u);

		default_vec4_xform_uniform_set = RD::get_singleton()->uniform_set_create(uniforms, default_shader_rd, RenderForwardClustered::TRANSFORMS_UNIFORM_SET);
	}
	{
		RD::SamplerState sampler;
		sampler.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.min_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.enable_compare = true;
		sampler.compare_op = RD::COMPARE_OP_GREATER;
		shadow_sampler = RD::get_singleton()->sampler_create(sampler);
	}
}

void SceneShaderForwardClustered::set_default_specialization(const ShaderSpecialization &p_specialization) {
	default_specialization = p_specialization;

	for (ShaderData &shader_data : shader_list) {
		shader_data.pipeline_hash_map.clear_pipelines();
	}
}

uint32_t SceneShaderForwardClustered::_get_variant_group(uint32_t p_features) {
	int64_t group = group_features.find(p_features);
	if (group < 0) {
		group = group_features.size();
		group_features.push_back(p_features);
	}
	return group;
}

void SceneShaderForwardClustered::enable_shader_features(uint32_t p_features) {
	if ((enabled_features.load(std::memory_order_relaxed) & p_features) == p_features) {
		return; // Called every frame; nothing new.
	}

	MutexLock lock(group_enable_mutex);
	uint32_t features = enabled_features.load() | p_features;
	enabled_features.store(features);
	for (uint32_t group = 0; group < group_features.size(); group++) {
		if ((group_features[group] & ~features) == 0) {
			shader.enable_group(group); // Does nothing if the group is enabled already.
		}
	}
}

void SceneShaderForwardClustered::_ensure_variant_group_enabled(uint16_t p_shader_version) {
	int group = shader.get_variant_to_group(p_shader_version);
	if (likely(shader.is_group_enabled(group))) {
		return;
	}

	// A pass requested a variant whose features were never enabled. Compile its
	// group now (one stall) instead of handing out a placeholder shader. The
	// renderer enables features before such passes; this is the safety net.
	MutexLock lock(group_enable_mutex);
	if (!shader.is_group_enabled(group)) {
		print_verbose(vformat("Forward+ shader variant %d requested before its features (0x%x) were enabled.", p_shader_version, group_features[group]));
		shader.enable_group(group);
	}
}

void SceneShaderForwardClustered::enable_multiview_shader_group() {
	enable_shader_features(SHADER_FEATURE_MULTIVIEW);
}

void SceneShaderForwardClustered::enable_advanced_shader_group(bool p_needs_multiview) {
	uint32_t features = SHADER_FEATURE_ALL_ADVANCED;
	if (p_needs_multiview || RendererCompositorRD::get_singleton()->is_xr_enabled()) {
		features |= SHADER_FEATURE_MULTIVIEW;
	}
	enable_shader_features(features);
}

bool SceneShaderForwardClustered::is_multiview_shader_group_enabled() const {
	return enabled_features.load(std::memory_order_relaxed) & SHADER_FEATURE_MULTIVIEW;
}

bool SceneShaderForwardClustered::is_advanced_shader_group_enabled(bool p_multiview) const {
	uint32_t features = SHADER_FEATURE_ALL_ADVANCED | (p_multiview ? SHADER_FEATURE_MULTIVIEW : 0);
	return (enabled_features.load(std::memory_order_relaxed) & features) == features;
}

uint32_t SceneShaderForwardClustered::get_pipeline_compilations(RSE::PipelineSource p_source) {
	MutexLock lock(SceneShaderForwardClustered::singleton_mutex);
	return pipeline_compilations[p_source];
}

RID SceneShaderForwardClustered::get_default_shader_rd(bool p_is_multiview) {
	RID &shader_rd = p_is_multiview ? default_multiview_shader_rd : default_shader_rd;

	if (shader_rd.is_null()) {
		RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
		ERR_FAIL_NULL_V(material_storage, RID());
		ERR_FAIL_COND_V(!default_material.is_valid(), RID());

		uint32_t flags = 0;
		if (p_is_multiview) {
			flags |= SHADER_COLOR_PASS_FLAG_MULTIVIEW;
		}

		MaterialData *md = static_cast<MaterialData *>(material_storage->material_get_data(default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		shader_rd = md->shader_data->get_shader_variant(PIPELINE_VERSION_COLOR_PASS, flags, false);
	}

	return shader_rd;
}
