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
#include "core/math/math_funcs_binary.h"
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
	// One row of the ray data texture per probe: 16384 is the texture limit.
	q.probes_per_frame = CLAMP(q.probes_per_frame, 64u, 16384u);
	q.irradiance_texels = CLAMP(q.irradiance_texels, 4u, 16u);
	q.distance_texels = CLAMP(q.distance_texels, 8u, 32u);
	q.gpu_time_budget_ms = MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/global_illumination/ddgi/gpu_time_budget_ms"));
	return q;
}

void RenderDDGI::ViewportData::free_data() {
	RD *rd = RD::get_singleton();
	RID *rids[] = { &data_buffer, &probe_buffer, &update_list, &stats_buffer, &ray_data, &irradiance_atlas, &irradiance_display, &distance_atlas };
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
	baked_version = 0;
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
		modes.push_back("\n#define MODE_SMOOTH\n");
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
	if (!p_render_data || p_render_data->reflection_probe.is_valid() || p_render_data->render_buffers.is_null()) {
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
	if (is_baked_only(p_render_data)) {
		return true; // Sampling baked probes is plain compute.
	}
	return p_render_data->rt_instances && RD::get_singleton()->has_feature(RD::SUPPORTS_RAYTRACING_PIPELINE);
}

bool RenderDDGI::baked_data_matches(const RendererEnvironmentStorage::DDGISettings &p_settings) {
	const Dictionary &d = p_settings.baked_data;
	if (p_settings.bake_mode == 0 || d.is_empty() || !p_settings.node_volume) {
		return false;
	}
	// The bake belongs to one volume: same probes, same place.
	return int(d.get("cascades", 0)) == p_settings.cascades &&
			Vector3i(d.get("probe_grid", Vector3i())) == p_settings.probe_grid &&
			Math::is_equal_approx(float(d.get("probe_spacing", 0.0)), p_settings.probe_spacing) &&
			Vector3(d.get("center", Vector3())).is_equal_approx(p_settings.volume_center);
}

bool RenderDDGI::is_baked_only(const RenderDataRD *p_render_data) {
	if (!p_render_data || p_render_data->environment.is_null()) {
		return false;
	}
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	if (s.bake_mode != 1 || s.baked_data.is_empty()) {
		return false;
	}
	if (!baked_data_matches(s)) {
		WARN_PRINT_ONCE("DDGI: the baked probe data doesn't match the DDGIVolume (size, position, probe spacing or cascades changed). Bake it again; until then DDGI updates the probes dynamically.");
		return false;
	}
	return true;
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

	// Lay probe tiles out in rows; the distance tiles are the larger ones.
	// Probes per row are a power of two (shaders use shifts, not divisions).
	uint32_t max_per_row = DDGI_MAX_ATLAS_SIZE / (p_quality.distance_texels + 2);
	uint32_t per_row = Math::next_power_of_2((uint32_t)Math::ceil(Math::sqrt((double)p_data->total_probes)));
	while (per_row > max_per_row) {
		per_row >>= 1;
	}
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
		float stats[4] = { 1.0f, 0, 0, 0 }; // rate_scale starts at 1.
		p_data->stats_buffer = rd->storage_buffer_create(sizeof(stats), Span<uint8_t>((uint8_t *)stats, sizeof(stats)));
		rd->set_resource_name(p_data->stats_buffer, "DDGI Stats");
	}

	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_2D;

	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = p_data->irradiance_size.x;
	tf.height = p_data->irradiance_size.y;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	p_data->irradiance_atlas = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->irradiance_atlas, "DDGI Irradiance Atlas");
	rd->texture_clear(p_data->irradiance_atlas, Color(0, 0, 0, 0), 0, 1, 0, 1);
	p_data->irradiance_display = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->irradiance_display, "DDGI Irradiance Display");
	rd->texture_clear(p_data->irradiance_display, Color(0, 0, 0, 0), 0, 1, 0, 1);

	tf.format = RD::DATA_FORMAT_R16G16_SFLOAT;
	tf.width = p_data->distance_size.x;
	tf.height = p_data->distance_size.y;
	p_data->distance_atlas = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->distance_atlas, "DDGI Distance Atlas");
	rd->texture_clear(p_data->distance_atlas, Color(0, 0, 0, 0), 0, 1, 0, 1);

	_allocate_rays(p_data, p_quality);
}

void RenderDDGI::_allocate_rays(ViewportData *p_data, const Quality &p_quality) {
	RD *rd = RD::get_singleton();
	for (RID *r : { &p_data->ray_data, &p_data->update_list }) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
	p_data->quality.rays_per_probe = p_quality.rays_per_probe;
	p_data->quality.probes_per_frame = p_quality.probes_per_frame;
	p_data->capacity = MIN(p_quality.probes_per_frame, p_data->total_probes);
	p_data->update_capacity_used = p_data->capacity;
	p_data->realtime_offset = 0;

	{
		Vector<uint8_t> init;
		init.resize(16 + p_data->capacity * 4);
		init.fill(0);
		p_data->update_list = rd->storage_buffer_create(init.size(), init);
		rd->set_resource_name(p_data->update_list, "DDGI Update List");
	}

	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_2D;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = p_quality.rays_per_probe;
	tf.height = p_data->capacity;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	p_data->ray_data = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(p_data->ray_data, "DDGI Ray Data");
}

uint32_t RenderDDGI::_setup_volumes(ViewportData *p_data, const RenderDataRD *p_render_data, DDGIDataGPU &r_gpu) {
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	const Vector3 cam_pos = p_render_data->scene_data->cam_transform.origin;
	const Vector3i grid = p_data->grid;
	const uint32_t probes_per_volume = uint32_t(grid.x * grid.y * grid.z);
	p_data->node_volume = s.node_volume;

	uint32_t count = 0;
	for (int c = 0; c < p_data->cascades && c < MAX_VOLUMES; c++) {
		ViewportData::VolumeState &vs = p_data->volumes[c];
		const float spacing = s.probe_spacing * float(1 << c);

		// Camera-following volumes snap to whole probes so their world positions
		// stay stable while scrolling. A DDGIVolume node instead owns the center;
		// moving or resizing it resets the probe data for that viewport.
		Vector3i origin;
		Vector3 node_center;
		if (s.node_volume) {
			node_center = s.volume_center;
			origin = Vector3i();
		} else {
			Vector3 cam_in_probes = cam_pos / spacing;
			Vector3i center((int32_t)Math::floor(cam_in_probes.x + 0.5f), (int32_t)Math::floor(cam_in_probes.y + 0.5f), (int32_t)Math::floor(cam_in_probes.z + 0.5f));
			origin = center - grid / 2;
		}

		int32_t *reset = r_gpu.volume_reset[c];
		reset[0] = reset[1] = reset[2] = reset[3] = 0;

		if (!vs.valid || vs.spacing != spacing || (s.node_volume && vs.center != node_center)) {
			vs.origin = origin;
			vs.scroll = Vector3i();
			vs.center = node_center;
			vs.spacing = spacing;
			vs.valid = true;
			reset[3] = 1;
		} else if (!s.node_volume && s.follow_camera && origin != vs.origin) {
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

		Vector3 center_world = s.node_volume ? s.volume_center : (Vector3(vs.origin) + (Vector3(grid) - Vector3(1, 1, 1)) * 0.5f) * spacing;
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
		// Realtime updates come every frame instead of every few frames: a
		// lower hysteresis per update gives about the same noise after the
		// display easing, and follows light changes much sooner.
		v.params[1] = s.realtime ? CLAMP(1.0f - (1.0f - s.hysteresis) * 2.0f, 0.0f, 0.999f) : s.hysteresis;
		// Coarser cascades cover distant, less detailed lighting: fewer updates.
		v.params[2] = 1.0f / float(1 << c);
		// Fade over the outermost probe of each cascade, so the next one takes over.
		v.params[3] = 1.0f;
		count++;
	}
	return count;
}

Ref<RenderDDGI::ViewportData> RenderDDGI::_prepare_viewport(RenderDataRD *p_render_data, Quality &r_quality) {
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	RendererEnvironmentStorage::DDGISettings s = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment);
	r_quality = get_quality();

	// Baked probes keep the atlas layout they were baked with; the rays and
	// probes per frame still come from the quality setting.
	const bool use_baked = baked_data_matches(s);
	if (use_baked) {
		r_quality.irradiance_texels = CLAMP(int(s.baked_data.get("irradiance_texels", r_quality.irradiance_texels)), 4, 16);
		r_quality.distance_texels = CLAMP(int(s.baked_data.get("distance_texels", r_quality.distance_texels)), 8, 32);
	}

	// Realtime updates: every probe every frame (in chunks of up to 16384,
	// the most one dispatch can hold), with fewer rays per update; the
	// hysteresis averages them over the frames.
	if (s.realtime) {
		r_quality.rays_per_probe = CLAMP(GLOBAL_GET_CACHED(int, "rendering/global_illumination/ddgi/realtime_rays_per_probe"), 16, 512);
		r_quality.probes_per_frame = 16384;
	}

	Ref<ViewportData> vd = _get_viewport_data(rb.ptr());
	const Quality &q = r_quality;
	if (vd->cascades != s.cascades || vd->grid != s.probe_grid || vd->quality.irradiance_texels != q.irradiance_texels ||
			vd->quality.distance_texels != q.distance_texels || !vd->data_buffer.is_valid()) {
		_allocate(vd.ptr(), s.cascades, s.probe_grid, q);
		if (!vd->data_buffer.is_valid()) {
			return Ref<ViewportData>();
		}
	} else if (vd->quality.rays_per_probe != q.rays_per_probe || vd->quality.probes_per_frame != q.probes_per_frame) {
		_allocate_rays(vd.ptr(), q);
	}

	if (!use_baked) {
		vd->baked_version = 0; // Upload again when the data fits again.
	} else if (vd->baked_version != s.baked_version) {
		if (!_upload_baked(vd.ptr(), s)) {
			vd->baked_version = s.baked_version; // Don't retry (and warn) every frame.
		}
	}
	return vd;
}

bool RenderDDGI::_upload_baked(ViewportData *p_data, const RendererEnvironmentStorage::DDGISettings &p_settings) {
	RD *rd = RD::get_singleton();
	const Dictionary &d = p_settings.baked_data;
	Ref<Image> irradiance = d.get("irradiance", Ref<Image>());
	Ref<Image> distance = d.get("distance", Ref<Image>());
	PackedFloat32Array probes = d.get("probes", PackedFloat32Array());

	ERR_FAIL_COND_V_MSG(irradiance.is_null() || distance.is_null(), false, "DDGI: the baked probe data has no atlases.");
	ERR_FAIL_COND_V_MSG(irradiance->get_format() != Image::FORMAT_RGBAH || irradiance->get_size() != p_data->irradiance_size, false, "DDGI: the baked irradiance atlas doesn't fit the probe layout. Bake again.");
	ERR_FAIL_COND_V_MSG(distance->get_format() != Image::FORMAT_RGH || distance->get_size() != p_data->distance_size, false, "DDGI: the baked distance atlas doesn't fit the probe layout. Bake again.");
	ERR_FAIL_COND_V_MSG(uint32_t(probes.size()) != p_data->total_probes * 4, false, "DDGI: the baked probe data has the wrong number of probes. Bake again.");

	rd->texture_update(p_data->irradiance_atlas, 0, irradiance->get_data());
	rd->texture_update(p_data->irradiance_display, 0, irradiance->get_data());
	rd->texture_update(p_data->distance_atlas, 0, distance->get_data());

	// Offsets and states from the bake. The probes count as updated (not new),
	// so dynamic updates blend into the baked light instead of replacing it.
	Vector<DDGIProbeGPU> gpu_probes;
	gpu_probes.resize(p_data->total_probes);
	DDGIProbeGPU *w = gpu_probes.ptrw();
	const float *r = probes.ptr();
	for (uint32_t i = 0; i < p_data->total_probes; i++) {
		w[i] = {};
		w[i].offset[0] = r[i * 4 + 0];
		w[i].offset[1] = r[i * 4 + 1];
		w[i].offset[2] = r[i * 4 + 2];
		w[i].state = uint32_t(r[i * 4 + 3]);
		w[i].last_update_frame = 1;
	}
	rd->buffer_update(p_data->probe_buffer, 0, gpu_probes.size() * sizeof(DDGIProbeGPU), gpu_probes.ptr());

	// The volumes are where the bake put them: no reset in _setup_volumes().
	for (int c = 0; c < p_data->cascades && c < MAX_VOLUMES; c++) {
		ViewportData::VolumeState &vs = p_data->volumes[c];
		vs.origin = Vector3i();
		vs.scroll = Vector3i();
		vs.center = p_settings.volume_center;
		vs.spacing = p_settings.probe_spacing * float(1 << c);
		vs.valid = true;
	}
	p_data->baked_version = p_settings.baked_version;
	return true;
}

void RenderDDGI::_write_frame_data(ViewportData *p_data, const RenderDataRD *p_render_data, const Quality &p_quality, uint32_t p_capacity, DDGIDataGPU &r_gpu) {
	RendererEnvironmentStorage *env_storage = RendererEnvironmentStorage::get_singleton();
	RID env = p_render_data->environment;
	RendererEnvironmentStorage::DDGISettings s = env_storage->environment_get_ddgi(env);
	const Quality &q = p_quality;

	uint32_t volume_count = _setup_volumes(p_data, p_render_data, r_gpu);

	// A random rotation per frame, so every update samples new directions.
	{
		Quaternion rot(p_data->rng.randf() * 2.0f - 1.0f, p_data->rng.randf() * 2.0f - 1.0f, p_data->rng.randf() * 2.0f - 1.0f, p_data->rng.randf() * 2.0f - 1.0f);
		if (rot.length_squared() < 0.0001f) {
			rot = Quaternion();
		}
		rot.normalize();
		Transform3D t = Transform3D(Basis(rot), Vector3());
		RendererRD::MaterialStorage::store_transform_transposed_3x4(t, r_gpu.ray_rotation);
	}

	{
		Projection correction;
		correction.set_depth_correction(true);
		Projection vp = correction * p_render_data->scene_data->cam_projection * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
		RendererRD::MaterialStorage::store_camera(vp, r_gpu.camera_view_projection);
		Vector3 cam_pos = p_render_data->scene_data->cam_transform.origin;
		r_gpu.camera_position[0] = cam_pos.x;
		r_gpu.camera_position[1] = cam_pos.y;
		r_gpu.camera_position[2] = cam_pos.z;
	}

	const uint32_t fixed_rays = MIN(32u, q.rays_per_probe / 4);
	r_gpu.counts[0] = volume_count;
	r_gpu.counts[1] = q.rays_per_probe;
	r_gpu.counts[2] = fixed_rays;
	r_gpu.counts[3] = p_capacity;
	r_gpu.atlas[0] = q.irradiance_texels;
	r_gpu.atlas[1] = q.distance_texels;
	r_gpu.atlas[2] = p_data->probes_per_row;
	r_gpu.atlas[3] = p_data->frame;
	r_gpu.atlas_inv_size[0] = 1.0f / p_data->irradiance_size.x;
	r_gpu.atlas_inv_size[1] = 1.0f / p_data->irradiance_size.y;
	r_gpu.atlas_inv_size[2] = 1.0f / p_data->distance_size.x;
	r_gpu.atlas_inv_size[3] = 1.0f / p_data->distance_size.y;
	r_gpu.schedule[0] = MIN(1.0f, float(p_capacity) / float(MAX(p_data->total_probes, 1u)));
	r_gpu.schedule[1] = float(p_data->total_probes);
	r_gpu.schedule[2] = DDGI_MAX_RAY_RADIANCE;
	r_gpu.schedule[3] = s.bounce_energy;
	// The display irradiance closes this share of its gap to the probes' each
	// frame: a time constant of light_transition_time at 60 frames per
	// second. Counted in frames, like the probe updates whose steps it hides.
	{
		float transition_frames = MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/global_illumination/ddgi/light_transition_time")) * 60.0f;
		r_gpu.smoothing[0] = transition_frames > 1.0f ? 1.0f - Math::exp(-1.0f / transition_frames) : 1.0f;
	}

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
		r_gpu.miss_color[0] = miss.r;
		r_gpu.miss_color[1] = miss.g;
		r_gpu.miss_color[2] = miss.b;
		r_gpu.miss_color[3] = use_sky ? 1.0f : 0.0f;
	}
}

void RenderDDGI::update_baked(RenderDataRD *p_render_data) {
	ERR_FAIL_COND(!initialized);
	Quality q;
	Ref<ViewportData> vd = _prepare_viewport(p_render_data, q);
	if (vd.is_null()) {
		return;
	}
	DDGIDataGPU gpu = {};
	_write_frame_data(vd.ptr(), p_render_data, q, 0, gpu);
	RD::get_singleton()->buffer_update(vd->data_buffer, 0, sizeof(DDGIDataGPU), &gpu);
}

Dictionary RenderDDGI::get_probe_data(RenderSceneBuffersRD *p_render_buffers) {
	ERR_FAIL_NULL_V(p_render_buffers, Dictionary());
	if (!p_render_buffers->has_custom_data(RB_SCOPE_DDGI)) {
		return Dictionary();
	}
	Ref<ViewportData> vd = p_render_buffers->get_custom_data(RB_SCOPE_DDGI);
	if (!vd->data_buffer.is_valid() || !vd->volumes[0].valid) {
		return Dictionary();
	}
	ERR_FAIL_COND_V_MSG(!vd->node_volume, Dictionary(), "DDGI: only a fixed DDGIVolume can be baked (turn off follow_camera).");

	RD *rd = RD::get_singleton();
	Vector<uint8_t> irradiance_bytes = rd->texture_get_data(vd->irradiance_atlas, 0);
	Vector<uint8_t> distance_bytes = rd->texture_get_data(vd->distance_atlas, 0);
	Vector<uint8_t> probe_bytes = rd->buffer_get_data(vd->probe_buffer);
	ERR_FAIL_COND_V(uint32_t(probe_bytes.size()) < vd->total_probes * sizeof(DDGIProbeGPU), Dictionary());

	PackedFloat32Array probes;
	probes.resize(vd->total_probes * 4);
	float *w = probes.ptrw();
	const DDGIProbeGPU *r = reinterpret_cast<const DDGIProbeGPU *>(probe_bytes.ptr());
	for (uint32_t i = 0; i < vd->total_probes; i++) {
		w[i * 4 + 0] = r[i].offset[0];
		w[i * 4 + 1] = r[i].offset[1];
		w[i * 4 + 2] = r[i].offset[2];
		w[i * 4 + 3] = float(r[i].state);
	}

	Dictionary d;
	d["cascades"] = vd->cascades;
	d["probe_grid"] = vd->grid;
	d["probe_spacing"] = vd->volumes[0].spacing;
	d["center"] = vd->volumes[0].center;
	d["irradiance_texels"] = int(vd->quality.irradiance_texels);
	d["distance_texels"] = int(vd->quality.distance_texels);
	d["irradiance"] = Image::create_from_data(vd->irradiance_size.x, vd->irradiance_size.y, false, Image::FORMAT_RGBAH, irradiance_bytes);
	d["distance"] = Image::create_from_data(vd->distance_size.x, vd->distance_size.y, false, Image::FORMAT_RGH, distance_bytes);
	d["probes"] = probes;
	return d;
}

void RenderDDGI::update_probes(RenderDataRD *p_render_data, RenderRaytracing *p_raytracing, RTViewportState *p_rt_state, uint32_t p_rt_flags) {
	ERR_FAIL_COND(!initialized);
	ERR_FAIL_NULL(p_raytracing);
	ERR_FAIL_NULL(p_rt_state);

	RD *rd = RD::get_singleton();
	Quality q;
	Ref<ViewportData> vd = _prepare_viewport(p_render_data, q);
	if (vd.is_null()) {
		return;
	}

	rd->draw_command_begin_label("DDGI Update Probes");

	vd->frame++;
	if (vd->frame == 0) {
		vd->frame = 1; // 0 means "never updated" in last_update_frame.
	}

	const bool realtime = RendererEnvironmentStorage::get_singleton()->environment_get_ddgi(p_render_data->environment).realtime;

	// GPU time budget: scale the probes traced per frame to the measured cost
	// per probe, between an eighth of the quality setting and all of it.
	// Realtime updates trace every probe instead.
	const bool use_budget = q.gpu_time_budget_ms > 0.0f && !realtime;
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

	const uint32_t capacity = vd->update_capacity_used;
	DDGIDataGPU gpu = {};
	_write_frame_data(vd.ptr(), p_render_data, q, capacity, gpu);

	rd->buffer_update(vd->data_buffer, 0, sizeof(DDGIDataGPU), &gpu);
	{
		// Empty list, or for realtime updates the full chunk (the scheduler
		// writes the slots directly).
		uint32_t header[4] = { realtime ? capacity : 0u, 0, 0, 0 };
		rd->buffer_update(vd->update_list, 0, sizeof(header), header);
	}

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
	RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
	RD::Uniform u_list(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, vd->update_list);
	RD::Uniform u_rays(RD::UNIFORM_TYPE_IMAGE, 3, vd->ray_data);
	RD::Uniform u_stats(RD::UNIFORM_TYPE_STORAGE_BUFFER, 5, vd->stats_buffer);
	RD::Uniform u_irradiance(RD::UNIFORM_TYPE_IMAGE, 4, vd->irradiance_atlas);
	RD::Uniform u_display(RD::UNIFORM_TYPE_IMAGE, 6, vd->irradiance_display);
	RD::Uniform u_distance(RD::UNIFORM_TYPE_IMAGE, 4, vd->distance_atlas);

	UpdatePushConstant push = {};
	push.total_probes = vd->total_probes;
	push.realtime = realtime ? 1 : 0;
	push.realtime_offset = vd->realtime_offset;
	if (realtime) {
		// Next frame continues after this chunk: neighboring probes (one
		// slab of the grid) update together, and every probe once per round.
		vd->realtime_offset = (vd->realtime_offset + capacity) % MAX(vd->total_probes, 1u);
	}

	// 1. Pick the probes to trace. This is split into two passes so full-reset
	// probes and scrolled-in probes get update slots before older probes spend
	// the frame budget. Scrolled probes keep their old irradiance as a fallback
	// until retraced, so fast camera movement shows stale GI instead of black.
	{
		_ddgi_timestamp("DDGI Schedule", use_budget);
		RD::ComputeListID list = rd->compute_list_begin();
		RID shader = update_shader.version_get_shader(update_shader_version, UPDATE_SCHEDULE);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_SCHEDULE]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_stats), 0);
		push.schedule_new_only = 1;
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, vd->total_probes, 1, 1);
		rd->compute_list_add_barrier(list);
		push.schedule_new_only = 0;
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, vd->total_probes, 1, 1);
		rd->compute_list_end();
	}

	// 2. Trace the probe rays with the path tracer's pipeline.
	{
		_ddgi_timestamp("DDGI Trace Probe Rays", false);
		RTDDGIBindings bindings;
		bindings.uniform_buffer = vd->data_buffer;
		bindings.irradiance_atlas = vd->irradiance_display;
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
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_irradiance, u_stats, u_display), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		// One group per list slot; groups past the list count exit at once.
		// (An indirect dispatch from the list count didn't see this frame's
		// count reliably, so the probes stopped updating.)
		rd->compute_list_dispatch(list, capacity, 1, 1);

		// Distances change only when geometry moves. With realtime updates
		// (every probe every frame) they are blended every 4th frame: about
		// half the blend cost, and moving objects still update within a few
		// frames.
		if (!realtime || (vd->frame & 3u) == 0u) {
			shader = update_shader.version_get_shader(update_shader_version, UPDATE_BLEND_DISTANCE);
			rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_BLEND_DISTANCE]);
			rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_distance, u_stats), 0);
			rd->compute_list_set_push_constant(list, &push, sizeof(push));
			rd->compute_list_dispatch(list, capacity, 1, 1);
		}

		rd->compute_list_end();

		// Classification changes the probe state the blend passes read; the
		// separate compute list orders it after them.
		_ddgi_timestamp("DDGI Relocate Classify", false);
		list = rd->compute_list_begin();
		shader = update_shader.version_get_shader(update_shader_version, UPDATE_RELOCATE_CLASSIFY);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_RELOCATE_CLASSIFY]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_stats), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, capacity, 1, 1);

		rd->compute_list_end();

		// 4. Ease the display irradiance (what surfaces sample) toward the
		// probes', every texel every frame.
		_ddgi_timestamp("DDGI Smooth", false);
		list = rd->compute_list_begin();
		shader = update_shader.version_get_shader(update_shader_version, UPDATE_SMOOTH);
		rd->compute_list_bind_compute_pipeline(list, update_pipelines[UPDATE_SMOOTH]);
		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_list, u_rays, u_irradiance, u_stats, u_display), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, vd->irradiance_size.x, vd->irradiance_size.y, 1);

		rd->compute_list_end();
	}

	rd->draw_command_end_label();
}

void RenderDDGI::apply(RenderDataRD *p_render_data, const RID *p_normal_roughness_slices, bool p_half_resolution) {
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
	// is on), including the half resolution setting
	// (rendering/global_illumination/gi/use_half_resolution).
	Size2i gi_size = p_half_resolution ? Size2i((internal_size.x + 1) / 2, (internal_size.y + 1) / 2) : internal_size;
	if (rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT)) {
		RD::TextureFormat f = rb->get_texture_format(RB_SCOPE_GI, RB_TEX_AMBIENT);
		if (f.width != uint32_t(gi_size.x) || f.height != uint32_t(gi_size.y)) {
			rb->clear_context(RB_SCOPE_GI);
		}
	}
	if (!rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT)) {
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		rb->create_texture(RB_SCOPE_GI, RB_TEX_AMBIENT, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage_bits, RD::TEXTURE_SAMPLES_1, gi_size);
		rb->create_texture(RB_SCOPE_GI, RB_TEX_REFLECTION, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage_bits, RD::TEXTURE_SAMPLES_1, gi_size);
		if (rb->has_custom_data(RB_SCOPE_GI)) {
			// Keep GI::process_gi's own bookkeeping in step.
			Ref<RendererRD::GI::RenderBuffersGI> rbgi = rb->get_custom_data(RB_SCOPE_GI);
			rbgi->using_half_size_gi = p_half_resolution;
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
		push.flags = p_half_resolution ? 1 : 0;
		push.debug_mode = uint32_t(s.debug_mode);

		RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
		RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
		RD::Uniform u_irradiance(RD::UNIFORM_TYPE_TEXTURE, 2, vd->irradiance_display);
		RD::Uniform u_distance(RD::UNIFORM_TYPE_TEXTURE, 3, vd->distance_atlas);
		RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 4, linear_sampler);
		RD::Uniform u_depth(RD::UNIFORM_TYPE_TEXTURE, 5, rb->get_depth_texture(v));
		RD::Uniform u_normal(RD::UNIFORM_TYPE_TEXTURE, 6, p_normal_roughness_slices[v]);
		RD::Uniform u_ambient(RD::UNIFORM_TYPE_IMAGE, 7, rb->get_texture_slice(RB_SCOPE_GI, RB_TEX_AMBIENT, v, 0));
		RD::Uniform u_reflection(RD::UNIFORM_TYPE_IMAGE, 8, rb->get_texture_slice(RB_SCOPE_GI, RB_TEX_REFLECTION, v, 0));

		rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader, 0, u_data, u_probes, u_irradiance, u_distance, u_sampler, u_depth, u_normal, u_ambient, u_reflection), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, gi_size.x, gi_size.y, 1);
	}

	rd->compute_list_end();
	rd->draw_command_end_label();
	// Closes the "DDGI Apply" interval for the profiler.
	_ddgi_timestamp("DDGI Done", false);
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
		push.flags = 0;
		push.debug_mode = uint32_t(s.debug_mode);

		RD::Uniform u_data(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, vd->data_buffer);
		RD::Uniform u_probes(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, vd->probe_buffer);
		RD::Uniform u_irradiance(RD::UNIFORM_TYPE_TEXTURE, 2, vd->irradiance_display);
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
