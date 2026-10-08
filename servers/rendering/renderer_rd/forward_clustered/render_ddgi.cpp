/**************************************************************************/
/*  render_ddgi.cpp                                                       */
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

#include "render_ddgi.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/environment/gi.h"
#include "servers/rendering/renderer_rd/forward_clustered/render_raytracing.h"
#include "servers/rendering/renderer_rd/forward_clustered/scene_shader_raytracing.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_default.h" // IWYU pragma: keep. RENDER_TIMESTAMP macro uses RSG.
#include "servers/rendering/storage/environment_storage.h"

using namespace RendererSceneRenderImplementation;

// DDGI pass timestamps: in the visual profiler / --gpu-profile like every
// RENDER_TIMESTAMP, and also when the GPU time budget needs them.
static void _ddgi_timestamp(const char *p_name, bool p_for_budget) {
	if (RSG::utilities->capturing_timestamps) {
		RSG::utilities->capture_timestamp(p_name);
	} else if (p_for_budget) {
		RD::get_singleton()->capture_timestamp(p_name);
	}
}

// GPU time (ms) from the "DDGI Schedule" timestamp to the next "DDGI Apply"
// in the newest resolved frame, or -1. Covers schedule, trace, blend,
// relocation and classification: the work that scales with the probe budget.
static float _ddgi_measured_update_ms() {
	RD *rd = RD::get_singleton();
	uint32_t count = rd->get_captured_timestamps_count();
	int64_t start = -1;
	for (uint32_t i = 0; i < count; i++) {
		String name = rd->get_captured_timestamp_name(i);
		if (start < 0 && name == "DDGI Schedule") {
			start = i;
		} else if (start >= 0 && name == "DDGI Apply") {
			uint64_t t0 = rd->get_captured_timestamp_gpu_time(start);
			uint64_t t1 = rd->get_captured_timestamp_gpu_time(i);
			return t1 > t0 ? float(double(t1 - t0) / 1000000.0) : -1.0f; // ns to ms.
		}
	}
	return -1.0f;
}

// Largest texture side we allow for the atlases.
static constexpr uint32_t DDGI_MAX_ATLAS_SIZE = 16384;
// Ray radiance above this luminance is scaled down before blending (fireflies).
static constexpr float DDGI_MAX_RAY_RADIANCE = 100.0f;

RenderDDGI::Quality RenderDDGI::get_quality() {
	// Rays per probe, probes traced per frame, irradiance and distance texels
	// per probe side. These are the workload controls; the presets only pick
	// values for them.
	static const Quality presets[4] = {
		{ 64, 1024, 6, 12, 0.0f }, // Low.
		{ 128, 2048, 6, 14, 0.0f }, // Medium.
		{ 192, 4096, 8, 14, 0.0f }, // High.
		{ 256, 8192, 8, 16, 0.0f }, // Ultra.
	};

	Quality q;
	int preset = GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/quality");
	if (preset >= 0 && preset < 4) {
		q = presets[preset];
	} else {
		q.rays_per_probe = GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/custom_rays_per_probe");
		q.probes_per_frame = GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/custom_probes_per_frame");
		q.irradiance_texels = GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/custom_irradiance_texels");
		q.distance_texels = GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/custom_distance_texels");
	}
	q.rays_per_probe = CLAMP(q.rays_per_probe, 32u, 512u);
	q.probes_per_frame = CLAMP(q.probes_per_frame, 64u, 65536u);
	q.irradiance_texels = CLAMP(q.irradiance_texels, 4u, 16u);
	q.distance_texels = CLAMP(q.distance_texels, 8u, 32u);
	q.gpu_time_budget_ms = MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/global_illumination/ddgi/gpu_time_budget_ms"));
	return q;
}

void RenderDDGI::ViewportData::free_data() {
	RD *rd = RD::get_singleton();
	RID *rids[] = { &data_buffer, &probe_buffer, &update_list, &ray_data, &irradiance_atlas, &distance_atlas };
	for (RID *r : rids) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
	for (VolumeState &v : volumes) {
		v.valid = false;
	}
	cascades = 0;
	total_probes = 0;
}

void RenderDDGI::initialize() {
	if (initialized) {
		return;
	}

	{
		Vector<String> modes;
		modes.push_back("\n#define MODE_SCHEDULE\n");
		modes.push_back("\n#define MODE_BLEND_IRRADIANCE\n");
		modes.push_back("\n#define MODE_BLEND_DISTANCE\n");
		modes.push_back("\n#define MODE_RELOCATE_CLASSIFY\n");
		update_shader.initialize(modes);
		update_shader_version = update_shader.version_create();
		for (int i = 0; i < UPDATE_MODE_MAX; i++) {
			update_pipelines[i] = RD::get_singleton()->compute_pipeline_create(update_shader.version_get_shader(update_shader_version, i));
		}
	}
	{
		Vector<String> modes;
		modes.push_back("\n#define MODE_APPLY\n");
		modes.push_back("\n#define MODE_DEBUG_PROBES\n");
		apply_shader.initialize(modes);
		apply_shader_version = apply_shader.version_create();
		for (int i = 0; i < APPLY_MODE_MAX; i++) {
			apply_pipelines[i] = RD::get_singleton()->compute_pipeline_create(apply_shader.version_get_shader(apply_shader_version, i));
		}
	}

	initialized = true;
}

RenderDDGI::~RenderDDGI() {
	if (!initialized) {
		return;
	}
	// Pipelines are freed together with their shader versions.
	update_shader.version_free(update_shader_version);
	apply_shader.version_free(apply_shader_version);
}

bool RenderDDGI::is_enabled_for(const RenderDataRD *p_render_data) {
	if (!p_render_data || p_render_data->reflection_probe.is_valid() || p_render_data->render_buffers.is_null() || !p_render_data->rt_instances) {
		return false;
	}
	RID env = p_render_data->environment;
	RendererEnvironmentStorage *env_storage = RendererEnvironmentStorage::get_singleton();
	if (env.is_null() || !env_storage->is_environment(env) || !env_storage->environment_get_ddgi_enabled(env)) {
		return false;
	}
	// The path tracer computes all indirect light itself; adding DDGI on top
	// would count it twice.
	if (env_storage->environment_get_pathtracing_enabled(env)) {
		return false;
	}
	return RD::get_singleton()->has_feature(RD::SUPPORTS_RAYTRACING_PIPELINE);
}

bool RenderDDGI::debug_shows_gi_buffer(const RenderDataRD *p_render_data) {
	if (!is_enabled_for(p_render_data)) {
		return false;
	}
	int mode = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment).debug_mode;
	return mode == DEBUG_INDIRECT_LIGHT || mode == DEBUG_CASCADES;
}

Ref<RenderDDGI::ViewportData> RenderDDGI::_get_viewport_data(RenderSceneBuffersRD *p_render_buffers) {
	if (p_render_buffers->has_custom_data(RB_SCOPE_DDGI)) {
		return p_render_buffers->get_custom_data(RB_SCOPE_DDGI);
	}
	Ref<ViewportData> data;
	data.instantiate();
	data->rng.seed(0x5eed0dd1);
	p_render_buffers->set_custom_data(RB_SCOPE_DDGI, data);
	return data;
}

void RenderDDGI::_allocate(ViewportData *p_data, int p_cascades, const Vector3i &p_grid, const Quality &p_quality) {
	RD *rd = RD::get_singleton();
	p_data->free_data();

	p_data->cascades = p_cascades;
	p_data->grid = p_grid;
	p_data->quality = p_quality;
	p_data->total_probes = uint32_t(p_cascades) * uint32_t(p_grid.x * p_grid.y * p_grid.z);
	p_data->capacity = MIN(p_quality.probes_per_frame, p_data->total_probes);
	p_data->update_capacity_used = p_data->capacity;

	// Lay probe tiles out in rows; the distance tiles are the larger ones.
	uint32_t max_per_row = DDGI_MAX_ATLAS_SIZE / (p_quality.distance_texels + 2);
	uint32_t per_row = CLAMP((uint32_t)Math::ceil(Math::sqrt((double)p_data->total_probes)), 1u, max_per_row);
	uint32_t rows = (p_data->total_probes + per_row - 1) / per_row;
	ERR_FAIL_COND_MSG(rows * (p_quality.distance_texels + 2) > DDGI_MAX_ATLAS_SIZE, "DDGI: too many probes for the atlas. Reduce Environment.ddgi_probe_grid or ddgi_cascades.");
	p_data->probes_per_row = per_row;
	p_data->irradiance_size = Size2i(per_row * (p_quality.irradiance_texels + 2), rows * (p_quality.irradiance_texels + 2));
	p_data->distance_size = Size2i(per_row * (p_quality.distance_texels + 2), rows * (p_quality.distance_texels + 2));

	p_data->data_buffer = rd->uniform_buffer_create(sizeof(DDGIDataGPU));
	rd->set_resource_name(p_data->data_buffer, "DDGI Data");

	{
		// All zero: every probe starts as DDGI_PROBE_NEW.
		Vector<uint8_t> zeros;
		zeros.resize(p_data->total_probes * sizeof(DDGIProbeGPU));
		zeros.fill(0);
		p_data->probe_buffer = rd->storage_buffer_create(zeros.size(), zeros);
		rd->set_resource_name(p_data->probe_buffer, "DDGI Probes");
	}
	{
		Vector<uint8_t> init;
		init.resize(16 + p_data->capacity * 4);
		init.fill(0);
		p_data->update_list = rd->storage_buffer_create(init.size(), init, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		rd->set_resource_name(p_data->update_list, "DDGI Update List");
	}

	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_2D;

	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = p_quality.rays_per_probe;
	tf.height = p_data->capacity;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT;
	p_data->ray_data = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->ray_data, "DDGI Ray Data");

	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = p_data->irradiance_size.x;
	tf.height = p_data->irradiance_size.y;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	p_data->irradiance_atlas = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->irradiance_atlas, "DDGI Irradiance Atlas");
	rd->texture_clear(p_data->irradiance_atlas, Color(0, 0, 0, 0), 0, 1, 0, 1);

	tf.format = RD::DATA_FORMAT_R16G16_SFLOAT;
	tf.width = p_data->distance_size.x;
	tf.height = p_data->distance_size.y;
	p_data->distance_atlas = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->distance_atlas, "DDGI Distance Atlas");
	rd->texture_clear(p_data->distance_atlas, Color(0, 0, 0, 0), 0, 1, 0, 1);
}

uint32_t RenderDDGI::_setup_volumes(ViewportData *p_data, const RenderDataRD *p_render_data, DDGIDataGPU &r_gpu) {
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	const Vector3 cam_pos = p_render_data->scene_data->cam_transform.origin;
	const Vector3i grid = p_data->grid;
	const uint32_t probes_per_volume = uint32_t(grid.x * grid.y * grid.z);

	uint32_t count = 0;
	for (int c = 0; c < p_data->cascades && c < MAX_VOLUMES; c++) {
		ViewportData::VolumeState &vs = p_data->volumes[c];
		const float spacing = s.probe_spacing * float(1 << c);

		// Center the grid on the camera, snapped to whole probes so the probes
		// keep their world positions while the volume scrolls.
		Vector3 cam_in_probes = cam_pos / spacing;
		Vector3i center((int32_t)Math::floor(cam_in_probes.x + 0.5f), (int32_t)Math::floor(cam_in_probes.y + 0.5f), (int32_t)Math::floor(cam_in_probes.z + 0.5f));
		Vector3i origin = center - grid / 2;

		int32_t *reset = r_gpu.volume_reset[c];
		reset[0] = reset[1] = reset[2] = reset[3] = 0;

		if (!vs.valid || vs.spacing != spacing) {
			vs.origin = origin;
			vs.scroll = Vector3i();
			vs.spacing = spacing;
			vs.valid = true;
			reset[3] = 1;
		} else if (s.follow_camera && origin != vs.origin) {
			Vector3i delta = origin - vs.origin;
			if (Math::abs(delta.x) >= grid.x || Math::abs(delta.y) >= grid.y || Math::abs(delta.z) >= grid.z) {
				// Teleported: nothing of the old grid is reusable.
				vs.scroll = Vector3i();
				reset[3] = 1;
			} else {
				vs.scroll = Vector3i(((vs.scroll.x + delta.x) % grid.x + grid.x) % grid.x,
						((vs.scroll.y + delta.y) % grid.y + grid.y) % grid.y,
						((vs.scroll.z + delta.z) % grid.z + grid.z) % grid.z);
				reset[0] = delta.x;
				reset[1] = delta.y;
				reset[2] = delta.z;
			}
			vs.origin = origin;
		}

		Vector3 center_world = (Vector3(vs.origin) + (Vector3(grid) - Vector3(1, 1, 1)) * 0.5f) * spacing;
		Transform3D local_to_world(Basis(), center_world);
		Transform3D world_to_local = local_to_world.affine_inverse();

		DDGIVolumeGPU &v = r_gpu.volumes[count];
		RendererRD::MaterialStorage::store_transform_transposed_3x4(world_to_local, v.world_to_local);
		RendererRD::MaterialStorage::store_transform_transposed_3x4(local_to_world, v.local_to_world);
		v.grid[0] = grid.x;
		v.grid[1] = grid.y;
		v.grid[2] = grid.z;
		v.grid[3] = int32_t(count * probes_per_volume);
		v.scroll[0] = vs.scroll.x;
		v.scroll[1] = vs.scroll.y;
		v.scroll[2] = vs.scroll.z;
		v.scroll[3] = (s.probe_relocation ? 1 : 0) | (s.probe_classification ? 2 : 0);
		v.spacing[0] = spacing;
		v.spacing[1] = s.normal_bias * spacing;
		v.spacing[2] = s.view_bias * spacing;
		v.spacing[3] = spacing * 1.75f; // A little more than the cell diagonal.
		v.params[0] = s.energy;
		v.params[1] = s.hysteresis;
		// Coarser cascades cover distant, less detailed lighting: fewer updates.
		v.params[2] = 1.0f / float(1 << c);
		// Fade over the outermost probe of each cascade, so the next one takes over.
		v.params[3] = 1.0f;
		count++;
	}
	return count;
}

void RenderDDGI::update_probes(RenderDataRD *p_render_data, RenderRaytracing *p_raytracing, RTViewportState *p_rt_state, uint32_t p_rt_flags) {
	ERR_FAIL_COND(!initialized);
	ERR_FAIL_NULL(p_raytracing);
	ERR_FAIL_NULL(p_rt_state);

	RD *rd = RD::get_singleton();
	RendererEnvironmentStorage *env_storage = RendererEnvironmentStorage::get_singleton();
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	RID env = p_render_data->environment;
	RendererEnvironmentStorage::DDGISettings s = env_storage->environment_get_ddgi(env);
	Quality q = get_quality();

	Ref<ViewportData> vd = _get_viewport_data(rb.ptr());
	if (vd->cascades != s.cascades || vd->grid != s.probe_grid || vd->quality.rays_per_probe != q.rays_per_probe ||
			vd->quality.probes_per_frame != q.probes_per_frame || vd->quality.irradiance_texels != q.irradiance_texels ||
			vd->quality.distance_texels != q.distance_texels || !vd->data_buffer.is_valid()) {
		_allocate(vd.ptr(), s.cascades, s.probe_grid, q);
		if (!vd->data_buffer.is_valid()) {
			return;
		}
	}

	rd->draw_command_begin_label("DDGI Update Probes");

	vd->frame++;
	if (vd->frame == 0) {
		vd->frame = 1; // 0 means "never updated" in last_update_frame.
	}

	// GPU time budget: scale the probes traced per frame to the measured cost
	// per probe, between an eighth of the quality setting and all of it.
	const bool use_budget = q.gpu_time_budget_ms > 0.0f;
	if (use_budget) {
		float measured_ms = _ddgi_measured_update_ms();
		if (measured_ms > 0.0f) {
			float per_probe_ms = measured_ms / float(MAX(vd->update_capacity_used, 1u));
			uint32_t min_probes = MAX(vd->capacity / 8, MIN(64u, vd->capacity));
			float target = CLAMP(q.gpu_time_budget_ms / per_probe_ms, float(min_probes), float(vd->capacity));
			// Move part of the way each frame: timings are noisy.
			vd->update_capacity_used = CLAMP((uint32_t)Math::round(Math::lerp(float(vd->update_capacity_used), target, 0.2f)), min_probes, vd->capacity);
		}
	} else {
		vd->update_capacity_used = vd->capacity;
	}

	DDGIDataGPU gpu = {};
	uint32_t volume_count = _setup_volumes(vd.ptr(), p_render_data, gpu);

	// A random rotation per frame, so every update samples new directions.
	{
		Quaternion rot(vd->rng.randf() * 2.0f - 1.0f, vd->rng.randf() * 2.0f - 1.0f, vd->rng.randf() * 2.0f - 1.0f, vd->rng.randf() * 2.0f - 1.0f);
		if (rot.length_squared() < 0.0001f) {
			rot = Quaternion();
		}
		rot.normalize();
		Transform3D t = Transform3D(Basis(rot), Vector3());
		RendererRD::MaterialStorage::store_transform_transposed_3x4(t, gpu.ray_rotation);
	}

	{
		Projection correction;
		correction.set_depth_correction(true);
		Projection vp = correction * p_render_data->scene_data->cam_projection * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
		RendererRD::MaterialStorage::store_camera(vp, gpu.camera_view_projection);
		Vector3 cam_pos = p_render_data->scene_data->cam_transform.origin;
		gpu.camera_position[0] = cam_pos.x;
		gpu.camera_position[1] = cam_pos.y;
		gpu.camera_position[2] = cam_pos.z;
	}

	const uint32_t capacity = vd->update_capacity_used;
	const uint32_t fixed_rays = MIN(32u, q.rays_per_probe / 4);
	gpu.counts[0] = volume_count;
	gpu.counts[1] = q.rays_per_probe;
	gpu.counts[2] = fixed_rays;
	gpu.counts[3] = capacity;
	gpu.atlas[0] = q.irradiance_texels;
	gpu.atlas[1] = q.distance_texels;
	gpu.atlas[2] = vd->probes_per_row;
	gpu.atlas[3] = vd->frame;
	gpu.atlas_inv_size[0] = 1.0f / vd->irradiance_size.x;
	gpu.atlas_inv_size[1] = 1.0f / vd->irradiance_size.y;
	gpu.atlas_inv_size[2] = 1.0f / vd->distance_size.x;
	gpu.atlas_inv_size[3] = 1.0f / vd->distance_size.y;
	gpu.schedule[0] = MIN(1.0f, float(capacity) / float(MAX(vd->total_probes, 1u)));
	gpu.schedule[1] = float(vd->total_probes);
	gpu.schedule[2] = DDGI_MAX_RAY_RADIANCE;

	// What rays that leave the scene see: the sky, or the ambient color when
	// there is no sky to sample.
	{
		RSE::EnvironmentAmbientSource ambient_source = env_storage->environment_get_ambient_source(env);
		RSE::EnvironmentBG bg = env_storage->environment_get_background(env);
		bool has_sky = env_storage->environment_get_sky(env).is_valid();
		Color miss;
		bool use_sky = false;
		if (ambient_source == RSE::ENV_AMBIENT_SOURCE_SKY || (ambient_source == RSE::ENV_AMBIENT_SOURCE_BG && bg == RSE::ENV_BG_SKY)) {
			use_sky = has_sky;
		} else if (ambient_source == RSE::ENV_AMBIENT_SOURCE_COLOR) {
			miss = env_storage->environment_get_ambient_light(env).srgb_to_linear() * env_storage->environment_get_ambient_light_energy(env);
		} else if (ambient_source == RSE::ENV_AMBIENT_SOURCE_BG && bg == RSE::ENV_BG_COLOR) {
			miss = env_storage->environment_get_bg_color(env).srgb_to_linear() * env_storage->environment_get_bg_energy_multiplier(env);
		}
		gpu.miss_color[0] = miss.r;
		gpu.miss_color[1] = miss.g;
		gpu.miss_color[2] = miss.b;
		gpu.miss_color[3] = use_sky ? 1.0f : 0.0f;
	}

	rd->buffer_update(vd->data_buffer, 0, sizeof(DDGIDataGPU), &gpu);
	{
		// Empty list; y and z are the group counts of the indirect blend dispatches.
		uint32_t header[4] = { 0, 1, 1, 0 };
		rd->buffer_update(vd->update_list, 0, sizeof(header), header);
	}

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
	RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
	RD::Uniform u_list(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, vd->update_list);
	RD::Uniform u_rays(RD::UNIFORM_TYPE_IMAGE, 3, vd->ray_data);
	RD::Uniform u_irradiance(RD::UNIFORM_TYPE_IMAGE, 4, vd->irradiance_atlas);
	RD::Uniform u_distance(RD::UNIFORM_TYPE_IMAGE, 4, vd->distance_atlas);

	UpdatePushConstant push = {};
	push.total_probes = vd->total_probes;

	// 1. Pick the probes to trace.
	{
		_ddgi_timestamp("DDGI Schedule", use_budget);
		RD::ComputeListID list = rd->compute_list_begin();
		RID shader = update_shader.version_get_shader(update_shader_version, UPDATE_SCHEDULE);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_SCHEDULE]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, vd->total_probes, 1, 1);
		rd->compute_list_end();
	}

	// 2. Trace the probe rays with the path tracer's pipeline.
	{
		_ddgi_timestamp("DDGI Trace Probe Rays", false);
		RTDDGIBindings bindings;
		bindings.uniform_buffer = vd->data_buffer;
		bindings.irradiance_atlas = vd->irradiance_atlas;
		bindings.distance_atlas = vd->distance_atlas;
		bindings.probe_buffer = vd->probe_buffer;
		bindings.ray_data = vd->ray_data;
		bindings.update_list = vd->update_list;
		bindings.trace = true;

		RID rt_set = p_raytracing->update_uniform_set(p_rt_state, p_render_data, p_rt_flags, &bindings);
		RID pipeline = p_raytracing->get_shader()->get_raytracing_pipeline(p_rt_flags);
		if (rt_set.is_valid() && pipeline.is_valid()) {
			RD::RaytracingListID list = rd->raytracing_list_begin();
			rd->raytracing_list_bind_raytracing_pipeline(list, pipeline);
			rd->raytracing_list_bind_uniform_set(list, rt_set, 0);
			RID bindless_set = p_raytracing->get_bindless_uniform_set();
			if (bindless_set.is_valid()) {
				rd->raytracing_list_bind_uniform_set(list, bindless_set, 1);
			}
			p_raytracing->register_raytracing_buffer_dependencies(list);
			rd->raytracing_list_trace_rays(list, 0, p_raytracing->get_shader()->get_hit_sbt(p_rt_flags), q.rays_per_probe, capacity, 1);
			rd->raytracing_list_end();
		}
	}

	// 3. Blend the rays into the probes, then relocate and classify them.
	{
		_ddgi_timestamp("DDGI Blend Probes", false);
		RD::ComputeListID list = rd->compute_list_begin();

		RID shader = update_shader.version_get_shader(update_shader_version, UPDATE_BLEND_IRRADIANCE);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_BLEND_IRRADIANCE]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_irradiance), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_indirect(list, vd->update_list, 0);

		shader = update_shader.version_get_shader(update_shader_version, UPDATE_BLEND_DISTANCE);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_BLEND_DISTANCE]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_distance), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_indirect(list, vd->update_list, 0);

		rd->compute_list_end();

		// Classification changes the probe state the blend passes read; the
		// separate compute list orders it after them.
		_ddgi_timestamp("DDGI Relocate Classify", false);
		list = rd->compute_list_begin();
		shader = update_shader.version_get_shader(update_shader_version, UPDATE_RELOCATE_CLASSIFY);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_RELOCATE_CLASSIFY]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, capacity, 1, 1);

		rd->compute_list_end();
	}

	rd->draw_command_end_label();
}

void RenderDDGI::apply(RenderDataRD *p_render_data, const RID *p_normal_roughness_slices) {
	ERR_FAIL_COND(!initialized);
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	ERR_FAIL_COND(rb.is_null());
	if (!rb->has_custom_data(RB_SCOPE_DDGI)) {
		return;
	}
	Ref<ViewportData> vd = rb->get_custom_data(RB_SCOPE_DDGI);
	if (!vd->data_buffer.is_valid()) {
		return;
	}

	RD *rd = RD::get_singleton();
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	Size2i internal_size = rb->get_internal_size();

	// The GI buffers are shared with SDFGI/VoxelGI (which are off while DDGI
	// is on). DDGI writes them at full resolution.
	if (rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT) && rb->get_texture_format(RB_SCOPE_GI, RB_TEX_AMBIENT).width != uint32_t(internal_size.x)) {
		rb->clear_context(RB_SCOPE_GI);
	}
	if (!rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT)) {
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		rb->create_texture(RB_SCOPE_GI, RB_TEX_AMBIENT, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage_bits, RD::TEXTURE_SAMPLES_1, internal_size);
		rb->create_texture(RB_SCOPE_GI, RB_TEX_REFLECTION, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage_bits, RD::TEXTURE_SAMPLES_1, internal_size);
		if (rb->has_custom_data(RB_SCOPE_GI)) {
			Ref<RendererRD::GI::RenderBuffersGI> rbgi = rb->get_custom_data(RB_SCOPE_GI);
			rbgi->using_half_size_gi = false; // So GI::process_gi recreates them if it needs half size.
		}
	}

	_ddgi_timestamp("DDGI Apply", get_quality().gpu_time_budget_ms > 0.0f);
	rd->draw_command_begin_label("DDGI Apply");

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RID linear_sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader = apply_shader.version_get_shader(apply_shader_version, APPLY_GI);

	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, apply_pipelines[APPLY_GI]);

	Projection correction;
	correction.set_depth_correction(true);

	for (uint32_t v = 0; v < p_render_data->scene_data->view_count; v++) {
		ApplyPushConstant push = {};
		RendererRD::MaterialStorage::store_camera((correction * p_render_data->scene_data->view_projection[v]).inverse(), push.inv_projection);
		RendererRD::MaterialStorage::store_transform_transposed_3x4(p_render_data->scene_data->cam_transform, push.cam_rotation);
		push.screen_size[0] = internal_size.x;
		push.screen_size[1] = internal_size.y;
		push.energy = s.energy;
		push.debug_mode = uint32_t(s.debug_mode);

		RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
		RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
		RD::Uniform u_irradiance(RD::UNIFORM_TYPE_TEXTURE, 2, vd->irradiance_atlas);
		RD::Uniform u_distance(RD::UNIFORM_TYPE_TEXTURE, 3, vd->distance_atlas);
		RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 4, linear_sampler);
		RD::Uniform u_depth(RD::UNIFORM_TYPE_TEXTURE, 5, rb->get_depth_texture(v));
		RD::Uniform u_normal(RD::UNIFORM_TYPE_TEXTURE, 6, p_normal_roughness_slices[v]);
		RD::Uniform u_ambient(RD::UNIFORM_TYPE_IMAGE, 7, rb->get_texture_slice(RB_SCOPE_GI, RB_TEX_AMBIENT, v, 0));
		RD::Uniform u_reflection(RD::UNIFORM_TYPE_IMAGE, 8, rb->get_texture_slice(RB_SCOPE_GI, RB_TEX_REFLECTION, v, 0));

		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_irradiance, u_distance, u_sampler, u_depth, u_normal, u_ambient, u_reflection), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, internal_size.x, internal_size.y, 1);
	}

	rd->compute_list_end();
	rd->draw_command_end_label();
}

void RenderDDGI::debug_draw(RenderDataRD *p_render_data) {
	if (!initialized || !is_enabled_for(p_render_data)) {
		return;
	}
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	if (s.debug_mode < DEBUG_PROBE_IRRADIANCE || s.debug_mode > DEBUG_PROBE_PRIORITY) {
		return;
	}
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	if (!rb->has_custom_data(RB_SCOPE_DDGI)) {
		return;
	}
	Ref<ViewportData> vd = rb->get_custom_data(RB_SCOPE_DDGI);
	if (!vd->data_buffer.is_valid() || rb->get_base_data_format() != RD::DATA_FORMAT_R16G16B16A16_SFLOAT) {
		return;
	}

	RD *rd = RD::get_singleton();
	Size2i internal_size = rb->get_internal_size();
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RID linear_sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID shader = apply_shader.version_get_shader(apply_shader_version, APPLY_DEBUG_PROBES);

	rd->draw_command_begin_label("DDGI Debug Probes");
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, apply_pipelines[APPLY_DEBUG_PROBES]);

	Projection correction;
	correction.set_depth_correction(true);

	for (uint32_t v = 0; v < p_render_data->scene_data->view_count; v++) {
		ApplyPushConstant push = {};
		RendererRD::MaterialStorage::store_camera((correction * p_render_data->scene_data->view_projection[v]).inverse(), push.inv_projection);
		RendererRD::MaterialStorage::store_transform_transposed_3x4(p_render_data->scene_data->cam_transform, push.cam_rotation);
		push.screen_size[0] = internal_size.x;
		push.screen_size[1] = internal_size.y;
		push.energy = s.energy;
		push.debug_mode = uint32_t(s.debug_mode);

		RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
		RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
		RD::Uniform u_irradiance(RD::UNIFORM_TYPE_TEXTURE, 2, vd->irradiance_atlas);
		RD::Uniform u_distance(RD::UNIFORM_TYPE_TEXTURE, 3, vd->distance_atlas);
		RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 4, linear_sampler);
		RD::Uniform u_depth(RD::UNIFORM_TYPE_TEXTURE, 5, rb->get_depth_texture(v));
		RD::Uniform u_color(RD::UNIFORM_TYPE_IMAGE, 6, rb->get_internal_texture(v));

		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_irradiance, u_distance, u_sampler, u_depth, u_color), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, internal_size.x, internal_size.y, 1);
	}

	rd->compute_list_end();
	rd->draw_command_end_label();
}
