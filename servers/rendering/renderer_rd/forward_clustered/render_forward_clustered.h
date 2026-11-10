/**************************************************************************/
/*  render_forward_clustered.h                                            */
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

#include "core/templates/paged_allocator.h"
#include "servers/rendering/multi_uma_buffer.h"
#include "servers/rendering/renderer_rd/cluster_builder_rd.h"
#include "servers/rendering/renderer_rd/effects/dlss.h"
#include "servers/rendering/renderer_rd/effects/fsr2.h"
#include "servers/rendering/renderer_rd/effects/motion_vectors_store.h"
#include "servers/rendering/renderer_rd/effects/planar_reflections.h"
#include "servers/rendering/renderer_rd/effects/ss_effects.h"
#include "servers/rendering/renderer_rd/effects/taa.h"
#include "servers/rendering/renderer_rd/forward_clustered/gpu_instance_slots.h"
#include "servers/rendering/renderer_rd/forward_clustered/gpu_instance_table.h"
#include "servers/rendering/renderer_rd/forward_clustered/render_raytracing.h"
#include "servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/best_fit_normal.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/integrate_dfg.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/mesh_clod_select.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/mesh_hiz_occlude.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/mesh_hiz_reduce.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/gpu_instance_cull.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/multimesh_cull.glsl.gen.h"

#ifdef METAL_ENABLED
#include "servers/rendering/renderer_rd/effects/metal_fx.h"
#endif

#define RB_SCOPE_FORWARD_CLUSTERED SNAME("forward_clustered")

#define RB_TEX_SPECULAR SNAME("specular")
#define RB_TEX_SPECULAR_MSAA SNAME("specular_msaa")
#define RB_TEX_NORMAL_ROUGHNESS SNAME("normal_roughness")
#define RB_TEX_NORMAL_ROUGHNESS_MSAA SNAME("normal_roughness_msaa")
#define RB_TEX_VOXEL_GI SNAME("voxel_gi")
#define RB_TEX_VOXEL_GI_MSAA SNAME("voxel_gi_msaa")
namespace RendererSceneRenderImplementation {

class RenderForwardClustered : public RendererSceneRenderRD {
	friend SceneShaderForwardClustered;
	friend SceneShaderRaytracing;
	friend class RenderRaytracing;

protected:
	enum {
		SCENE_UNIFORM_SET = 0,
		RENDER_PASS_UNIFORM_SET = 1,
		TRANSFORMS_UNIFORM_SET = 2,
		MATERIAL_UNIFORM_SET = 3,
	};

	enum {
		SDFGI_MAX_CASCADES = 8,
		MAX_VOXEL_GI_INSTANCESS = 8,
		MAX_LIGHTMAPS = 8,
		MAX_VOXEL_GI_INSTANCESS_PER_INSTANCE = 2,
		INSTANCE_DATA_BUFFER_MIN_SIZE = 4096
	};

	enum RenderListType {
		RENDER_LIST_OPAQUE, //used for opaque objects
		RENDER_LIST_MOTION, //used for opaque objects with motion
		RENDER_LIST_ALPHA, //used for transparent objects
		RENDER_LIST_SECONDARY, //used for shadows and other objects
		RENDER_LIST_MAX
	};

	/* Scene Shader */

	SceneShaderForwardClustered scene_shader;

public:
	static constexpr uint32_t GEOMETRY_STATISTICS_WORDS = 29;
	// MultiMesh culling words of shadow passes start here (same layout as the main view's).
	static constexpr uint32_t GEOMETRY_STATISTICS_SHADOW_OFFSET = 12;
	virtual Dictionary gpu_geometry_get_statistics(const Ref<RenderSceneBuffers> &p_render_buffers) override;

	/* Framebuffer */

	class RenderBufferDataForwardClustered : public RenderBufferCustomDataRD {
		GDCLASS(RenderBufferDataForwardClustered, RenderBufferCustomDataRD)

	private:
		RenderSceneBuffersRD *render_buffers = nullptr;
		RendererRD::FSR2Context *fsr2_context = nullptr;
		RendererRD::DLSSContext *dlss_context = nullptr;
#ifdef METAL_MFXTEMPORAL_ENABLED
		RendererRD::MFXTemporalContext *mfx_temporal_context = nullptr;
#endif

	public:
		ClusterBuilderRD *cluster_builder = nullptr;

		// Per-viewport GPU outputs. RD orders writes after prior submitted readers and
		// defers destruction; source MultiMesh transforms and commands are never replaced.
		struct MultiMeshCullBuffers {
			RID visible_ids;
			RID commands;
			RID inputs;
			RID ranks;
			RID blocks;
			uint32_t id_capacity = 0;
			uint32_t draw_capacity = 0;
			bool active = false;
			// Per dispatched draw, for HZB occlusion after the depth pre-pass.
			struct Draw {
				RID transforms;
				uint32_t count = 0;
				Transform3D transform;
			};
			LocalVector<Draw> draws;
			LocalVector<uint32_t> command_words; // As uploaded to `commands`.
			// HZB occlusion (main view only): the frustum-visible instances that
			// are not behind the depth pre-pass, compacted into a second ID and
			// command buffer that only the opaque colour passes use.
			RID occluded_ids;
			RID occluded_commands;
			RID occlusion_inputs;
			uint32_t occluded_id_capacity = 0;
			uint32_t occluded_draw_capacity = 0;
			uint32_t occlusion_input_capacity = 0;
			bool occlusion_ready = false;
		};
		// Main view and shadow passes keep separate buffers: shadow draws run
		// between the main view's culling dispatch and its opaque draws.
		MultiMeshCullBuffers multimesh_cull;
		MultiMeshCullBuffers multimesh_shadow_cull;
		struct MeshCLODBuffers {
			RID commands;
			RID inputs;
			uint32_t group_capacity = 0;
			uint32_t draw_capacity = 0;
			RID occlusion_inputs;
			uint32_t occlusion_capacity = 0;
			bool active = false;
			// Two-pass occlusion: phase-1 and phase-2 depth commands (same layout
			// as `commands`), and one history word per group. History slots are
			// keyed by surface cache and kept while the surface keeps its group
			// count; a stale slot only costs a phase-2 draw.
			RID depth_commands;
			RID phase2_commands;
			uint32_t pass_command_capacity = 0;
			RID history;
			uint32_t history_capacity = 0;
			uint32_t history_used = 0;
			struct HistorySlot {
				uint32_t offset = 0;
				uint32_t count = 0;
			};
			HashMap<uint64_t, HistorySlot> history_slots;
			struct OcclusionWork {
				RID groups;
				uint32_t count = 0;
			};
			LocalVector<OcclusionWork> occlusion_work;
			bool occlusion_ready = false;
			bool two_pass = false;
		} mesh_clod;
		// Depth pyramid shared by CLOD and MultiMesh HZB occlusion, built once
		// per frame from the (phase-1) depth pre-pass.
		struct HZB {
			uint32_t mipmaps = 0;
			bool ready = false;
		} hzb;
		// GPU geometry counters, see RenderingServer::viewport_get_gpu_geometry_statistics.
		// Kernels always accumulate into `buffer`; it is cleared and read back
		// asynchronously only while a caller polled recently. CPU counts are per frame.
		struct GeometryStatistics {
			RID buffer;
			uint64_t requested_until_frame = 0;
			bool recording = false;
			uint32_t culled_surfaces = 0;
			uint32_t skipped_surfaces = 0;
			uint64_t skipped_instances = 0;
			uint32_t clod_surfaces = 0;
			uint32_t shadow_culled_surfaces = 0;
			Dictionary latest;
		} geometry_statistics;

		struct SSEffectsData {
			Projection ssil_last_frame_projections[RendererSceneRender::MAX_RENDER_VIEWS];
			Transform3D ssil_last_frame_transform;

			Projection ssr_last_frame_projections[RendererSceneRender::MAX_RENDER_VIEWS];
			Transform3D ssr_last_frame_transform;

			RendererRD::SSEffects::SSILRenderBuffers ssil;
			RendererRD::SSEffects::SSAORenderBuffers ssao;
			RendererRD::SSEffects::SSRRenderBuffers ssr;
			RendererRD::SSEffects::SSCSRenderBuffers sscs;
		} ss_effects_data;

		enum DepthFrameBufferType {
			DEPTH_FB,
			DEPTH_FB_ROUGHNESS,
			DEPTH_FB_ROUGHNESS_VOXELGI
		};

		RID render_sdfgi_uniform_set;

		void ensure_specular();
		bool has_specular() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR); }
		RID get_specular() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR); }
		RID get_specular(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR, p_layer, 0); }
		RID get_specular_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR_MSAA, p_layer, 0); }

		void ensure_normal_roughness_texture();
		bool has_normal_roughness() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }
		RID get_normal_roughness() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }
		RID get_normal_roughness(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS, p_layer, 0); }
		RID get_normal_roughness_msaa() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS_MSAA); }
		RID get_normal_roughness_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS_MSAA, p_layer, 0); }

		void ensure_voxelgi();
		bool has_voxelgi() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI); }
		RID get_voxelgi() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI); }
		RID get_voxelgi(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI, p_layer, 0); }
		RID get_voxelgi_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI_MSAA, p_layer, 0); }

		void ensure_fsr2(RendererRD::FSR2Effect *effect);
		RendererRD::FSR2Context *get_fsr2_context() const { return fsr2_context; }

		void ensure_dlss(RendererRD::DLSSEffect *effect);
		RendererRD::DLSSContext *get_dlss_context() const { return dlss_context; }

#ifdef METAL_MFXTEMPORAL_ENABLED
		bool ensure_mfx_temporal(RendererRD::MFXTemporalEffect *p_effect);
		RendererRD::MFXTemporalContext *get_mfx_temporal_context() const { return mfx_temporal_context; }
#endif

		RID get_color_only_fb();
		RID get_color_pass_fb(uint32_t p_color_pass_flags);
		RID get_depth_fb(DepthFrameBufferType p_type = DEPTH_FB);
		RID get_specular_only_fb();
		RID get_velocity_only_fb();

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override;
		virtual void free_data() override;

		static RD::DataFormat get_specular_format();
		static uint32_t get_specular_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
		static RD::DataFormat get_normal_roughness_format();
		static uint32_t get_normal_roughness_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
		static RD::DataFormat get_voxelgi_format();
		static uint32_t get_voxelgi_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
	};

protected:
	virtual void setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) override;

	RID render_base_uniform_set;

	uint64_t lightmap_texture_array_version = 0xFFFFFFFF;

	void _update_render_base_uniform_set();
	RID _setup_sdfgi_render_pass_uniform_set(RID p_albedo_texture, RID p_emission_texture, RID p_emission_aniso_texture, RID p_geom_facing_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index);
	RID _setup_render_pass_uniform_set(RenderListType p_render_list, const RenderDataRD *p_render_data, bool p_is_multiview, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index, bool p_use_directional_shadow_atlas = false, RID p_visible_ids = RID(), RID p_gpu_instances = RID(), RID p_gpu_instance_ids = RID());
	void _prepare_multimesh_culling(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	void _prepare_multimesh_shadow_culling(RenderBufferDataForwardClustered *p_buffers);
	// TAA and upscalers shift NDC x/y by the jitter (Projection::add_jitter_offset).
	// Dividing the x/y rows by 1 + |jitter| widens the frustum to |ndc| <= 1 + |jitter|,
	// which contains the jittered one, so frustum tests stay exact for the raster.
	static Projection _jitter_safe_projection(const Projection &p_projection, const Vector2 &p_jitter) {
		Projection projection = p_projection;
		const Vector2 jitter = p_jitter.abs();
		if (jitter != Vector2()) {
			for (int column = 0; column < 4; column++) {
				projection.columns[column][0] /= 1.0 + jitter.x;
				projection.columns[column][1] /= 1.0 + jitter.y;
			}
		}
		return projection;
	}
	struct RenderElementInfo;
	// One record per culled surface; matches CullData in multimesh_cull.glsl.
	struct MultiMeshCullInput {
		float planes[6][4];
		float center[4];
		float half_extents[4];
		uint32_t count;
		uint32_t stride;
		uint32_t current_offset;
		uint32_t ids_offset;
		float lod_camera[4];
		float lod_edges[4];
		uint32_t lod_levels;
		uint32_t lod_stride;
		uint32_t first_command;
		uint32_t statistics_offset;
	};
	// A camera or shadow view to cull against.
	struct MultiMeshCullView {
		Vector<Plane> planes;
		Vector3 lod_camera;
		float lod_threshold = 0.0f;
		float lod_distance_multiplier = 1.0f;
		bool lod_allowed = false;
		uint32_t statistics_offset = 0;
		// Dispatch MultiMeshes that lie fully inside the frustum too, so that HZB
		// occlusion can test their instances.
		bool keep_fully_inside = false;
	};
	// Per-frame work list; kept as a member so its storage is reused.
	struct MultiMeshCullBatch {
		LocalVector<MultiMeshCullInput> inputs;
		LocalVector<RID> transforms;
		LocalVector<RenderElementInfo *> elements;
		LocalVector<uint32_t> last_lods;
		LocalVector<Transform3D> node_transforms;
		LocalVector<uint32_t> commands;
		uint32_t total_ids = 0;
		uint32_t skipped_surfaces = 0;
		uint64_t skipped_instances = 0;
		void clear() {
			inputs.clear();
			transforms.clear();
			elements.clear();
			last_lods.clear();
			node_transforms.clear();
			commands.clear();
			total_ids = 0;
			skipped_surfaces = 0;
			skipped_instances = 0;
		}
	} multimesh_cull_batch;
	bool _multimesh_culling_available(const RenderBufferDataForwardClustered *p_buffers) const;
	void _collect_multimesh_culling(RenderListType p_list, uint32_t p_from, uint32_t p_count, const MultiMeshCullView &p_view, bool p_shadow, MultiMeshCullBatch &r_batch);
	bool _dispatch_multimesh_culling(MultiMeshCullBatch &p_batch, RenderBufferDataForwardClustered::MultiMeshCullBuffers &r_buffers, RID p_statistics, const String &p_label);
	// GPU MultiMesh LOD uses at most four buckets over the index LODs the
	// MultiMesh bounds span: from the CPU LOD of their nearest point (bucket 0)
	// to that of their farthest point. With a wider span, bucket b maps to
	// p_first + round(b * span / 3). An instance may get a finer LOD than the
	// per-instance CPU rule would pick, never coarser.
	static _FORCE_INLINE_ uint32_t _multimesh_lod_for_bucket(uint32_t p_bucket, uint32_t p_first, uint32_t p_last) {
		const uint32_t span = p_last - p_first;
		return p_first + (span < 4 ? p_bucket : (p_bucket * span + 1) / 3);
	}
	struct MultiMeshCull {
		MultimeshCullShaderRD shader;
		RID version;
		RID pipelines[5]; // Cull, scan, scatter, HZB occlusion, scan without statistics.
	} multimesh_cull;
	void _prepare_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	struct MeshCLOD {
		MeshClodSelectShaderRD shader;
		RID version;
		RID pipelines[2]; // Resident selection, and residency-aware selection for streamed surfaces.
		bool multi_draw = false;
	} mesh_clod;
	// HZB shared by CLOD and MultiMesh occlusion. _hzb_possible() is the cheap
	// part of the conditions, known before culling; _setup_hzb() runs before the
	// depth pre-pass and _build_hzb() after it (phase 1 with two passes).
	bool _hzb_possible(const RenderDataRD *p_render_data, const RenderBufferDataForwardClustered *p_buffers) const;
	bool _setup_hzb(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	void _build_hzb(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	// HZB occlusion of GPU-culled MultiMesh instances, for the opaque colour
	// passes; the depth pre-pass keeps the frustum-culled instances.
	bool _setup_multimesh_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	void _occlude_multimesh(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	// HZB occlusion of resident CLOD groups. Setup runs before the depth
	// pre-pass; with two passes the split restricts the pre-pass to groups
	// visible when last tested, and after the HZB test the recovered groups
	// are drawn into depth before anything reads it.
	bool _setup_mesh_clod_occlusion(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	void _split_mesh_clod_occlusion(RenderBufferDataForwardClustered *p_buffers);
	void _occlude_mesh_clod(const RenderDataRD *p_render_data, RenderBufferDataForwardClustered *p_buffers);
	void _begin_geometry_statistics(RenderBufferDataForwardClustered *p_buffers);
	void _end_geometry_statistics(RenderBufferDataForwardClustered *p_buffers);
	static void _receive_geometry_statistics(const Vector<uint8_t> &p_data, uint64_t p_buffers, uint32_t p_culled_surfaces, uint32_t p_skipped_surfaces, uint64_t p_skipped_instances, uint32_t p_clod_surfaces, uint32_t p_shadow_culled_surfaces, uint64_t p_frame);
	struct MeshHiZ {
		MeshHizReduceShaderRD reduce_shader;
		RID reduce_version;
		RID reduce_pipelines[2];
		MeshHizOccludeShaderRD occlude_shader;
		RID occlude_version;
		RID occlude_pipeline;
		RID split_pipeline;
	} mesh_hiz;

	struct RenderListParameters;
	struct GeometryInstanceSurfaceDataCache;

	struct BestFitNormal {
		BestFitNormalShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} best_fit_normal;

	struct IntegrateDFG {
		IntegrateDfgShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} dfg_lut;

	struct LTC {
		RID lut1_texture;
		RID lut2_texture;
	} ltc;

	enum PassMode {
		PASS_MODE_COLOR,
		PASS_MODE_SHADOW,
		PASS_MODE_SHADOW_DP,
		PASS_MODE_DEPTH,
		PASS_MODE_DEPTH_NORMAL_ROUGHNESS,
		PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI,
		PASS_MODE_DEPTH_MATERIAL,
		PASS_MODE_SDF,
		PASS_MODE_MAX
	};

	enum ColorPassFlags {
		COLOR_PASS_FLAG_TRANSPARENT = 1 << 0,
		COLOR_PASS_FLAG_SEPARATE_SPECULAR = 1 << 1,
		COLOR_PASS_FLAG_MULTIVIEW = 1 << 2,
		COLOR_PASS_FLAG_MOTION_VECTORS = 1 << 3,
	};

	struct RenderElementInfo;

	struct RenderListParameters {
		GeometryInstanceSurfaceDataCache **elements = nullptr;
		RenderElementInfo *element_info = nullptr;
		int element_count = 0;
		bool reverse_cull = false;
		PassMode pass_mode = PASS_MODE_COLOR;
		uint32_t color_pass_flags = 0;
		bool no_gi = false;
		uint32_t view_count = 1;
		RID render_pass_uniform_set;
		bool force_wireframe = false;
		Vector2 uv_offset;
		float lod_distance_multiplier = 0.0;
		float screen_mesh_lod_threshold = 0.0;
		RD::FramebufferFormatID framebuffer_format = 0;
		uint32_t element_offset = 0;
		bool use_directional_soft_shadow = false;
		SceneShaderForwardClustered::ShaderSpecialization base_specialization = {};
		bool use_material_feedback = false;
		RID multimesh_cull_commands;
		RID mesh_clod_commands;
		bool mesh_clod_only = false; // Draw only CLOD elements (phase-2 depth of two-pass occlusion).
		RID gpu_instance_commands; // GPU path draw buckets' indirect commands.

		RenderListParameters(GeometryInstanceSurfaceDataCache **p_elements, RenderElementInfo *p_element_info, int p_element_count, bool p_reverse_cull, PassMode p_pass_mode, uint32_t p_color_pass_flags, bool p_no_gi, bool p_use_directional_soft_shadows, RID p_render_pass_uniform_set, bool p_force_wireframe = false, const Vector2 &p_uv_offset = Vector2(), float p_lod_distance_multiplier = 0.0, float p_screen_mesh_lod_threshold = 0.0, uint32_t p_view_count = 1, uint32_t p_element_offset = 0, SceneShaderForwardClustered::ShaderSpecialization p_base_specialization = {}, bool p_use_material_feedback = false) {
			elements = p_elements;
			element_info = p_element_info;
			element_count = p_element_count;
			reverse_cull = p_reverse_cull;
			pass_mode = p_pass_mode;
			color_pass_flags = p_color_pass_flags;
			no_gi = p_no_gi;
			view_count = p_view_count;
			render_pass_uniform_set = p_render_pass_uniform_set;
			force_wireframe = p_force_wireframe;
			uv_offset = p_uv_offset;
			lod_distance_multiplier = p_lod_distance_multiplier;
			screen_mesh_lod_threshold = p_screen_mesh_lod_threshold;
			element_offset = p_element_offset;
			use_directional_soft_shadow = p_use_directional_soft_shadows;
			base_specialization = p_base_specialization;
			use_material_feedback = p_use_material_feedback;
		}
	};

	struct LightmapData {
		float normal_xform_and_specular_intensity[12];
		float texture_size[2];
		float exposure_normalization;
		uint32_t flags;
	};

	struct LightmapCaptureData {
		float sh[9 * 4];
	};

	// When changing any of these enums, remember to change the corresponding enums in the shader files as well.
	enum {
		INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT = 1 << 2,
		INSTANCE_DATA_FLAGS_DYNAMIC = 1 << 3,
		INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE = 1 << 4,
		INSTANCE_DATA_FLAG_USE_GI_BUFFERS = 1 << 5,
		INSTANCE_DATA_FLAG_USE_SDFGI = 1 << 6,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP_CAPTURE = 1 << 7,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP = 1 << 8,
		INSTANCE_DATA_FLAG_USE_SH_LIGHTMAP = 1 << 9,
		INSTANCE_DATA_FLAG_USE_VOXEL_GI = 1 << 10,
		INSTANCE_DATA_FLAG_PARTICLES = 1 << 11,
		INSTANCE_DATA_FLAG_MULTIMESH = 1 << 12,
		INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D = 1 << 13,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR = 1 << 14,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA = 1 << 15,
		INSTANCE_DATA_FLAGS_PARTICLE_TRAIL_SHIFT = 16,
		INSTANCE_DATA_FLAGS_PARTICLE_TRAIL_MASK = 0xFF,
		INSTANCE_DATA_FLAGS_FADE_SHIFT = 24,
		INSTANCE_DATA_FLAGS_FADE_MASK = 0xFFUL << INSTANCE_DATA_FLAGS_FADE_SHIFT
	};

	// When changing any of these enums, remember to change the corresponding enums in the shader files as well.
	enum {
		SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO = (1 << 0),
		SCREEN_SPACE_EFFECTS_FLAGS_USE_SSIL = (1 << 1),
		SCREEN_SPACE_EFFECTS_FLAGS_USE_SSR = (1 << 2),
		SCREEN_SPACE_EFFECTS_FLAGS_RESOLVE_SSR = (1 << 3),
		SCREEN_SPACE_EFFECTS_FLAGS_USE_SSCS = (1 << 4),
		SCREEN_SPACE_EFFECTS_FLAGS_USE_PLANAR_REFLECTIONS = (1 << 5),
	};

	enum {
		REFLECTION_FLAGS_DDGI_OVERRIDES_PROBE_AMBIENT = (1 << 0),
		REFLECTION_FLAGS_DDGI_IN_PROBE_CAPTURE = (1 << 1), // Probe capture: sample the DDGI probes directly (bindings 42-45).
		REFLECTION_FLAGS_PROBES_CAPTURED_WITH_DDGI = (1 << 2), // The view's probes were captured with DDGI's light.
	};

	struct SceneState {
		// This struct is loaded into Set 1 - Binding 1, populated at start of rendering a frame, must match with shader code
		struct UBO {
			uint32_t cluster_shift;
			uint32_t cluster_width;
			uint32_t cluster_type_size;
			uint32_t max_cluster_element_count_div_32;

			uint32_t ss_effects_flags;
			float ssao_light_affect;
			float ssao_ao_affect;
			uint32_t pad1;

			float sdf_to_bounds[16];

			int32_t sdf_offset[3];
			uint32_t pad2;

			int32_t sdf_size[3];
			uint32_t gi_upscale_for_msaa;

			uint32_t volumetric_fog_enabled;
			float volumetric_fog_inv_length;
			float volumetric_fog_detail_spread;
			uint32_t volumetric_fog_pad;

			float reflection_normalization;
			float reflection_normalization_max;
			uint32_t reflection_flags;
			uint32_t reflection_pad;
		};

		struct PushConstantUbershader {
			SceneShaderForwardClustered::ShaderSpecialization specialization;
			SceneShaderForwardClustered::UbershaderConstants constants;
		};

		struct PushConstant {
			uint32_t base_index; //
			uint32_t uv_offset; //packed
			uint32_t multimesh_motion_vectors_current_offset;
			uint32_t multimesh_motion_vectors_previous_offset;
			uint32_t multimesh_visible_ids_offset;
			PushConstantUbershader ubershader;
		};

		struct InstanceData {
			float transform[12];
			float compressed_aabb_position[4];
			float compressed_aabb_size[3];
			uint32_t material_feedback_index;
			float uv_scale[4];
			uint32_t flags;
			uint32_t instance_uniforms_ofs; //base offset in global buffer for instance variables
			uint32_t gi_offset; //GI information when using lightmapping (VCT or lightmap index)
			uint32_t layer_mask;
			float prev_transform[12];
			float lightmap_uv_scale[4];
#ifdef REAL_T_IS_DOUBLE
			float model_precision[4];
			float prev_model_precision[4];
#endif

			// These setters allow us to copy the data over with operation when using floats.
			inline void set_lightmap_uv_scale(const Rect2 &p_rect) {
#ifdef REAL_T_IS_DOUBLE
				lightmap_uv_scale[0] = p_rect.position.x;
				lightmap_uv_scale[1] = p_rect.position.y;
				lightmap_uv_scale[2] = p_rect.size.x;
				lightmap_uv_scale[3] = p_rect.size.y;
#else
				Rect2 *rect = reinterpret_cast<Rect2 *>(lightmap_uv_scale);
				*rect = p_rect;
#endif
			}

			inline void set_compressed_aabb(const AABB &p_aabb) {
#ifdef REAL_T_IS_DOUBLE
				compressed_aabb_position[0] = p_aabb.position.x;
				compressed_aabb_position[1] = p_aabb.position.y;
				compressed_aabb_position[2] = p_aabb.position.z;

				compressed_aabb_size[0] = p_aabb.size.x;
				compressed_aabb_size[1] = p_aabb.size.y;
				compressed_aabb_size[2] = p_aabb.size.z;
#else
				Vector3 *compressed_aabb_position_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_position);
				Vector3 *compressed_aabb_size_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_size);
				*compressed_aabb_position_vec3 = p_aabb.position;
				*compressed_aabb_size_vec3 = p_aabb.size;
#endif
			}

			inline void set_uv_scale(const Vector4 &p_uv_scale) {
#ifdef REAL_T_IS_DOUBLE
				uv_scale[0] = p_uv_scale.x;
				uv_scale[1] = p_uv_scale.y;
				uv_scale[2] = p_uv_scale.z;
				uv_scale[3] = p_uv_scale.w;
#else
				Vector4 *uv_scale_vec4 = reinterpret_cast<Vector4 *>(uv_scale);
				*uv_scale_vec4 = p_uv_scale;
#endif
			}
		};

		static_assert(std::is_trivially_destructible_v<InstanceData>);
		static_assert(std::is_trivially_constructible_v<InstanceData>);

		UBO ubo;

		LocalVector<RID> uniform_buffers;
		LocalVector<RID> implementation_uniform_buffers;
		uint32_t used_uniform_buffer_count = 0;

		LightmapData lightmaps[MAX_LIGHTMAPS];
		RID lightmap_ids[MAX_LIGHTMAPS];
		bool lightmap_has_sh[MAX_LIGHTMAPS];
		bool lightmap_has_specular[MAX_LIGHTMAPS];
		uint32_t lightmaps_used = 0;
		uint32_t max_lightmaps;
		RID lightmap_buffer;

		MultiUmaBuffer<1u> instance_buffer[RENDER_LIST_MAX] = { MultiUmaBuffer<1u>("RENDER_LIST_OPAQUE"), MultiUmaBuffer<1u>("RENDER_LIST_MOTION"), MultiUmaBuffer<1u>("RENDER_LIST_ALPHA"), MultiUmaBuffer<1u>("RENDER_LIST_SECONDARY") };
		InstanceData *curr_gpu_ptr[RENDER_LIST_MAX] = {};

		LightmapCaptureData *lightmap_captures = nullptr;
		uint32_t max_lightmap_captures;
		RID lightmap_capture_buffer;

		RID voxelgi_ids[MAX_VOXEL_GI_INSTANCESS];
		uint32_t voxelgis_used = 0;

		bool used_screen_texture = false;
		bool used_normal_texture = false;
		bool used_depth_texture = false;
		bool used_sss = false;
		bool used_lightmap = false;
		bool used_opaque_stencil = false;

		struct ShadowPass {
			uint32_t element_from;
			uint32_t element_count;
			PassMode pass_mode;

			RID rp_uniform_set;
			float lod_distance_multiplier;
			float screen_mesh_lod_threshold;

			RID framebuffer;
			Rect2i rect;
			bool clear_depth;
			bool flip_cull;

			uint32_t uniform_buffer_index;

			// Inputs of MultiMesh shadow culling.
			Projection cam_projection;
			Transform3D cam_transform;
			Transform3D main_cam_transform;
		};

		LocalVector<ShadowPass> shadow_passes;
		RID shadow_multimesh_cull_commands; // Valid while shadow passes use GPU culling.

		void grow_instance_buffer(RenderListType p_render_list, uint32_t p_req_element_count, bool p_append);
	} scene_state;

	static RenderForwardClustered *singleton;

	uint32_t _setup_environment(const RenderDataRD *p_render_data, bool p_no_fog, const Size2i &p_screen_size, const Size2 &p_viewport_size, const Color &p_default_bg_color, bool p_opaque_render_buffers = false, bool p_apply_alpha_multiplier = false, bool p_pancake_shadows = false);
	void _setup_voxelgis(const PagedArray<RID> &p_voxelgis);
	void _setup_lightmaps(const RenderDataRD *p_render_data, const PagedArray<RID> &p_lightmaps, const Transform3D &p_cam_transform);
	static uint32_t _count_directional_lights(const RenderDataRD *p_render_data);

	struct RenderElementInfo {
		enum { MAX_REPEATS = (1 << 20) - 1 };
		union {
			struct {
				uint32_t lod_index : 8;
				uint32_t uses_softshadow : 1;
				uint32_t uses_projector : 1;
				uint32_t uses_forward_gi : 1;
				uint32_t uses_lightmap : 1;

				// This value is not part of the sort key as there are no more available bits.
				uint32_t uses_lightmap_specular : 1;
				// GPU path: draws the members that moved; the color pass skips it unless it writes motion vectors.
				uint32_t gpu_motion : 1;
			};
			uint32_t value;
		};
		uint32_t repeat;
		uint32_t multimesh_cull_draw; // First indirect command record.
		union {
			uint32_t multimesh_visible_ids_offset;
			uint32_t gpu_depth_bucket; // GPU path: the indirect command of the depth pre-pass (shadow mesh index counts).
		};
		uint32_t multimesh_cull_lod; // GPU LOD level count << 24 | visible IDs reserved per level.
		uint32_t multimesh_cull_last_lod; // Index LOD of the last GPU LOD level; level 0 is lod_index.
		uint32_t mesh_clod_offset;
		uint32_t gpu_bucket; // GPU path draw bucket (UINT32_MAX: none); its indirect command.
		uint32_t gpu_first; // The bucket's first record in the GPU path's instance data.
	};

	static_assert(std::is_trivially_destructible_v<RenderElementInfo>);
	static_assert(std::is_trivially_constructible_v<RenderElementInfo>);

	template <PassMode p_pass_mode, uint32_t p_color_pass_flags = 0>
	_FORCE_INLINE_ void _render_list_template(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	void _render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	void _render_list_with_draw_list(RenderListParameters *p_params, RID p_framebuffer, BitField<RD::DrawFlags> p_draw_flags = RD::DRAW_DEFAULT_ALL, const Vector<Color> &p_clear_color_values = Vector<Color>(), float p_clear_depth_value = 0.0, uint32_t p_clear_stencil_value = 0, const Rect2 &p_region = Rect2());

	void _fill_instance_data(RenderListType p_render_list, int *p_render_info = nullptr, uint32_t p_offset = 0, int32_t p_max_elements = -1, bool p_update_buffer = true);
	void _fill_render_list(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_using_sdfgi = false, bool p_using_opaque_gi = false, bool p_using_motion_pass = false, bool p_append = false, bool p_alpha_only = false);

	HashMap<Size2i, RID> sdfgi_framebuffer_size_cache;

	struct GeometryInstanceData;
	class GeometryInstanceForwardClustered;

	struct GeometryInstanceLightmapSH {
		Color sh[9];
	};

	// Cached data for drawing surfaces
	struct GeometryInstanceSurfaceDataCache {
		enum {
			FLAG_PASS_DEPTH = 1,
			FLAG_PASS_OPAQUE = 2,
			FLAG_PASS_ALPHA = 4,
			FLAG_PASS_SHADOW = 8,
			FLAG_USES_SHARED_SHADOW_MATERIAL = 128,
			FLAG_USES_SUBSURFACE_SCATTERING = 2048,
			FLAG_USES_SCREEN_TEXTURE = 4096,
			FLAG_USES_DEPTH_TEXTURE = 8192,
			FLAG_USES_NORMAL_TEXTURE = 16384,
			FLAG_USES_DOUBLE_SIDED_SHADOWS = 32768,
			FLAG_USES_PARTICLE_TRAILS = 65536,
			FLAG_USES_MOTION_VECTOR = 131072,
			FLAG_USES_STENCIL = 262144,
		};

		union {
			struct {
				uint64_t sort_key1;
				uint64_t sort_key2;
			};
			struct {
				// Needs to be grouped together to be used in RenderElementInfo, as the value is masked directly.
				uint64_t lod_index : 8;
				uint64_t uses_softshadow : 1;
				uint64_t uses_projector : 1;
				uint64_t uses_forward_gi : 1;
				uint64_t uses_lightmap : 1;
				// "uses_lightmap_specular" is excluded as there are no more available bits.

				// Sorted based on optimal order for respecting priority and reducing the amount of rebinding of shaders, materials,
				// and geometry. This current order was found to be the most optimal in large projects. If you wish to measure
				// differences, refer to RenderingDeviceGraph and the methods available to print statistics for draw lists.
				uint64_t depth_layer : 4;
				uint64_t surface_index : 8;
				uint64_t geometry_id : 32;
				uint64_t material_id_hi : 8;

				uint64_t material_id_lo : 24;
				uint64_t shader_id : 32;
				uint64_t priority : 8;
			};
		} sort;

		RSE::PrimitiveType primitive = RSE::PRIMITIVE_MAX;
		uint32_t flags = 0;
		uint32_t rt_pass_flags = 0;
		uint32_t surface_index = 0;
		bool uses_lightmap_specular = false;
		uint32_t color_pass_inclusion_mask = 0;

		void *surface = nullptr;
		RID material_uniform_set;
		SceneShaderForwardClustered::ShaderData *shader = nullptr;
		SceneShaderForwardClustered::MaterialData *material = nullptr;

		void *surface_shadow = nullptr;
		RID material_uniform_set_shadow;
		SceneShaderForwardClustered::ShaderData *shader_shadow = nullptr;

		mutable Transform3D cached_final_transform;
		mutable bool cached_final_transform_valid = false;

		mutable RID rt_deformed_handle;

		GeometryInstanceSurfaceDataCache *next = nullptr;
		GeometryInstanceForwardClustered *owner = nullptr;
		SelfList<GeometryInstanceSurfaceDataCache> compilation_dirty_element;
		SelfList<GeometryInstanceSurfaceDataCache> compilation_all_element;

		GeometryInstanceSurfaceDataCache() :
				compilation_dirty_element(this), compilation_all_element(this) {}
	};

	class GeometryInstanceForwardClustered : public RenderGeometryInstanceBase {
	public:
		/// Heap-allocated procedural RT state. Only created when the instance is procedural.
		RTProceduralState *rt_procedural = nullptr;

		// lightmap
		RID lightmap_instance;
		Rect2 lightmap_uv_scale;
		uint32_t lightmap_slice_index;
		GeometryInstanceLightmapSH *lightmap_sh = nullptr;

		//used during rendering

		uint32_t gi_offset_cache = 0;
		bool store_transform_cache = true;
		RID transforms_uniform_set;
		uint32_t instance_count = 0;
		uint32_t trail_steps = 1;
		bool can_sdfgi = false;
		bool using_projectors = false;
		bool using_softshadows = false;

		//used during setup
		uint32_t gpu_slot = GPUInstanceSlots::INVALID; // Index in the GPU instance table.
		// GPU path (gpu_instances.cpp): the scenario the scene cull reported, the
		// one whose draw buckets hold the instance (0: none) and its index there.
		uint64_t gpu_scenario = 0;
		uint64_t gpu_member_scenario = 0;
		uint32_t gpu_member_index = UINT32_MAX;
		uint32_t gpu_pending_index = UINT32_MAX; // In gpu_instances_pending until the next cull.
		uint32_t gpu_surface_count = 0;
		uint64_t prev_transform_change_frame = 0xFFFFFFFF;
		uint64_t last_aged_frame = 0;
		enum TransformStatus {
			NONE,
			MOVED,
			TELEPORTED,
		} transform_status = TransformStatus::MOVED;
		Transform3D prev_transform;
		RID voxel_gi_instances[MAX_VOXEL_GI_INSTANCESS_PER_INSTANCE];
		GeometryInstanceSurfaceDataCache *surface_caches = nullptr;
		SelfList<GeometryInstanceForwardClustered> dirty_list_element;

		GeometryInstanceForwardClustered() :
				dirty_list_element(this) {}

		virtual void _mark_dirty() override;

		virtual void set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) override;
		virtual void set_layer_mask(uint32_t p_layer_mask) override;
		virtual void set_lod_bias(float p_lod_bias) override;
		virtual void reset_motion_vectors() override;
		virtual void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override;
		virtual void set_lightmap_capture(const Color *p_sh9) override;

		RTProceduralState *_ensure_procedural_state();
		void _free_procedural_state();

		virtual void set_rt_procedural(bool p_procedural, const AABB &p_aabb) override;
		virtual void set_rt_procedural_bounds(const Vector<float> &p_aabb_data, bool p_expose_bounds) override;

		virtual void clear_light_instances() override {}
		virtual void pair_light_instance(const RID p_light_instance, RSE::LightType light_type, uint32_t placement_idx) override {}
		virtual void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override {}
		virtual void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		virtual void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override;

		virtual void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override;

		virtual void set_gpu_scenario(uint64_t p_scenario) override;
		virtual void set_transparency(float p_transparency) override;

		void age_out_motion(uint64_t p_frame);
	};

	// These are not used in the Forward+ path, it has different light clustering tech.
	virtual uint32_t get_max_lights_total() override { return 0; }
	virtual uint32_t get_max_lights_per_mesh() override { return 0; }

	static void _geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker);
	static void _geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker);

	SelfList<GeometryInstanceForwardClustered>::List geometry_instance_dirty_list;
	SelfList<GeometryInstanceSurfaceDataCache>::List geometry_surface_compilation_dirty_list;
	SelfList<GeometryInstanceSurfaceDataCache>::List geometry_surface_compilation_all_list;

	PagedAllocator<GeometryInstanceForwardClustered> geometry_instance_alloc;
	GPUInstanceSlots gpu_instance_slots;

	/* GPU instance table (rendering/geometry/gpu_instance_table/*) */

	GPUInstanceTable gpu_instance_table;
	LocalVector<GeometryInstanceForwardClustered *> gpu_instance_owners; // Indexed by slot; null when free.
	bool gpu_instance_table_active = false;
	uint64_t gpu_instance_table_frame = UINT64_MAX; // Last frame a validation was requested.
	// Validation: every element _fill_instance_data writes is compared with
	// its table record (counts reported with the buffer check).
	bool gpu_instance_table_checking = false;
	uint32_t gpu_instance_table_elements_checked = 0;
	uint32_t gpu_instance_table_elements_failed = 0;
	void _gpu_instance_table_check_element(const GeometryInstanceForwardClustered *p_instance, const SceneState::InstanceData &p_data);

	_FORCE_INLINE_ void _gpu_instance_table_mark(uint32_t p_slot) {
		if (gpu_instance_table_active) {
			gpu_instance_table.mark_dirty(p_slot);
		}
	}
	void _gpu_instance_table_build_record(uint32_t p_slot, GPUInstanceTable::Record &r_record) const;
	void _update_gpu_instance_table();

	/* GPU path for eligible instances (rendering/geometry/gpu_instances/*, gpu_instances.cpp) */

	// Instance surfaces drawn alike (render-element sort key without depth
	// layer, LOD and per-view GI bits, plus mirror) form a bucket: one
	// indirect draw per index LOD, whose instances a compute pass culls, gives
	// a LOD and writes per view.
	struct GPUInstanceBucket {
		GeometryInstanceSurfaceDataCache *surface = nullptr; // Drawn with this surface's state.
		uint32_t first = 0; // First member.
		uint32_t count = 0;
		uint32_t levels = 1; // Index LODs of the surface, including the full mesh.
		// Indirect commands, one per level for members that did not move, then
		// one per level for those that moved (drawn by the motion pass in views
		// with one; the others put every member in the first set).
		uint32_t command = 0;
		uint32_t depth_command = UINT32_MAX; // Same for the depth pre-pass when it draws a separate shadow mesh.
		uint32_t ids_first = 0; // Visible IDs, count reserved per command, in the same order.
	};
	// Bucket in gpu_instance_cull.glsl.
	struct GPUInstanceBucketData {
		float compressed_aabb_position[3];
		uint32_t ids_first;
		float compressed_aabb_size[3];
		uint32_t material_feedback_index;
		float uv_scale[4];
		uint32_t command;
		uint32_t count;
		uint32_t lod_count; // Reduced index LODs (levels - 1).
		uint32_t lod_edges_first; // Their edge lengths in the LOD edges buffer.
		uint32_t depth_command; // UINT32_MAX: the depth pre-pass uses command.
		uint32_t pad[3];
	};
	struct GPUInstanceScenario {
		// Members, including those that join at the next cull (not gpu_drawn yet).
		LocalVector<GeometryInstanceForwardClustered *> instances;
		bool dirty = true; // Buckets need a rebuild.
		LocalVector<GPUInstanceBucket> buckets;
		LocalVector<GPUInstanceBucketData> bucket_data;
		LocalVector<uint32_t> members; // Table slot and bucket per drawn instance surface.
		LocalVector<uint32_t> commands; // Five words per bucket level, zero instances.
		LocalVector<uint8_t> command_levels; // Index LOD of each command, plus GPU_INSTANCE_MOVED_COMMAND (validation).
		LocalVector<uint32_t> command_depth; // Per command, its depth pre-pass twin or UINT32_MAX (UINT32_MAX - 1 for the twins).
		LocalVector<float> lod_edges; // Screen-space edge length per reduced LOD.
		uint32_t member_count = 0;
		uint32_t id_count = 0;
		RID members_buffer;
		RID buckets_buffer;
		RID commands_buffer;
		RID lod_edges_buffer;
		RID output_buffer; // InstanceData per member, written per view.
		RID ids_buffer; // Members drawn per bucket level, written per view.
		uint32_t member_capacity = 0;
		uint32_t bucket_capacity = 0;
		uint32_t command_capacity = 0;
		uint32_t id_capacity = 0;
		uint32_t lod_edge_capacity = 0;
	};
	HashMap<uint64_t, GPUInstanceScenario *> gpu_instance_scenarios;
	bool gpu_instances_active = false; // rendering/geometry/gpu_instances/enabled as applied.
	// Instances that left the GPU path during this render while the cull had
	// listed them for it: the CPU list draws them this time.
	LocalVector<GeometryInstanceForwardClustered *> gpu_instances_left;
	// Members that start being drawn by the GPU path at the next camera cull,
	// so that the cull and the draw buckets agree within a render.
	LocalVector<GeometryInstanceForwardClustered *> gpu_instances_pending;
	LocalVector<GeometryInstanceForwardClustered *> gpu_instances_left_view;
	struct GPUInstanceCull {
		GpuInstanceCullShaderRD shader;
		RID version;
		RID pipeline;
	} gpu_instance_cull;
	// Validation (rendering/geometry/gpu_instances/validate).
	uint64_t gpu_instances_check_frame = UINT64_MAX;
	bool gpu_instances_check_pending = false;
	uint32_t gpu_instances_check_expected = 0;
	uint32_t gpu_instances_check_buckets = 0;
	uint32_t gpu_instances_check_members = 0;
	uint32_t gpu_instances_check_levels[8] = {}; // Expected surfaces per index LOD (the last counts the rest).
	uint32_t gpu_instances_check_moved = 0; // Expected surfaces drawn by the motion pass.
	LocalVector<uint8_t> gpu_instances_check_command_levels;
	LocalVector<uint32_t> gpu_instances_check_command_depth;
	static void _gpu_instances_check_received(const Vector<uint8_t> &p_data);

	bool _gpu_instance_eligible(const GeometryInstanceForwardClustered *p_instance) const;
	void _gpu_instance_refresh(GeometryInstanceForwardClustered *p_instance, bool p_rendering);
	void _gpu_instance_leave(GeometryInstanceForwardClustered *p_instance);
	void _gpu_instances_update_setting();
	void _gpu_instances_rebuild(GPUInstanceScenario &p_scenario);
	// Before the camera render list: decides whether the GPU path draws this
	// view and, if so, culls and writes its instances (gpu_instances_view).
	bool _gpu_instances_begin_view(RenderDataRD *p_render_data, bool p_using_motion_pass, bool p_using_sdfgi, bool p_using_opaque_gi);
	// After the CPU elements are prepared: one element per bucket.
	void _gpu_instances_append(int *p_render_info, float p_lod_threshold);
	GPUInstanceScenario *gpu_instances_view = nullptr; // Drawn by the current camera render.
	bool gpu_instances_view_motion = false; // The view has a motion pass, and members may have moved.
	uint64_t gpu_instances_moved_frame = 0; // Last frame an instance moved (stays moved for one more frame).
	void _gpu_instances_free();
	PagedAllocator<GeometryInstanceSurfaceDataCache> geometry_instance_surface_alloc;
	PagedAllocator<GeometryInstanceLightmapSH> geometry_instance_lightmap_sh;

	struct SurfacePipelineData {
		void *mesh_surface = nullptr;
		void *mesh_surface_shadow = nullptr;
		SceneShaderForwardClustered::ShaderData *shader = nullptr;
		SceneShaderForwardClustered::ShaderData *shader_shadow = nullptr;
		bool instanced = false;
		bool uses_opaque = false;
		bool uses_transparent = false;
		bool uses_depth = false;
		bool can_use_lightmap = false;
	};

	struct GlobalPipelineData {
		union {
			uint32_t key;

			struct {
				uint32_t texture_samples : 3;
				uint32_t use_reflection_probes : 1;
				uint32_t use_separate_specular : 1;
				uint32_t use_motion_vectors : 1;
				uint32_t use_normal_and_roughness : 1;
				uint32_t use_lightmaps : 1;
				uint32_t use_voxelgi : 1;
				uint32_t use_sdfgi : 1;
				uint32_t use_multiview : 1;
				uint32_t use_16_bit_shadows : 1;
				uint32_t use_32_bit_shadows : 1;
				uint32_t use_shadow_cubemaps : 1;
				uint32_t use_shadow_dual_paraboloid : 1;
			};
		};
	};

	GlobalPipelineData global_pipeline_data_compiled = {};
	GlobalPipelineData global_pipeline_data_required = {};

	typedef Pair<SceneShaderForwardClustered::ShaderData *, SceneShaderForwardClustered::ShaderData::PipelineKey> ShaderPipelinePair;

	void _update_global_pipeline_data_requirements_from_project();
	void _update_global_pipeline_data_requirements_from_light_storage();
	void _geometry_instance_add_surface_with_material(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh);
	void _geometry_instance_add_surface_with_material_chain(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, RID p_mat_src, RID p_mesh);
	void _geometry_instance_add_surface(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, RID p_material, RID p_mesh);
	void _geometry_instance_update(RenderGeometryInstance *p_geometry_instance);
	void _mesh_compile_pipeline_for_surface(SceneShaderForwardClustered::ShaderData *p_shader, void *p_mesh_surface, bool p_ubershader, bool p_instanced_surface, RSE::PipelineSource p_source, SceneShaderForwardClustered::ShaderData::PipelineKey &r_pipeline_key, Vector<ShaderPipelinePair> *r_pipeline_pairs = nullptr);
	void _mesh_compile_pipelines_for_surface(const SurfacePipelineData &p_surface, const GlobalPipelineData &p_global, RSE::PipelineSource p_source, Vector<ShaderPipelinePair> *r_pipeline_pairs = nullptr);
	void _mesh_generate_all_pipelines_for_surface_cache(GeometryInstanceSurfaceDataCache *p_surface_cache, const GlobalPipelineData &p_global);
	void _update_dirty_geometry_instances();
	void _update_dirty_geometry_pipelines();

	// Global data about the scene that can be used to pre-allocate resources without relying on culling.
	struct GlobalSurfaceData {
		bool screen_texture_used = false;
		bool normal_texture_used = false;
		bool depth_texture_used = false;
		bool sss_used = false;
	} global_surface_data;

	/* Render List */

	struct RenderList {
		LocalVector<GeometryInstanceSurfaceDataCache *> elements;
		LocalVector<RenderElementInfo> element_info;

		void clear() {
			elements.clear();
			element_info.clear();
		}

		//should eventually be replaced by radix

		struct SortByKey {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.sort_key2 == B->sort.sort_key2) ? (A->sort.sort_key1 < B->sort.sort_key1) : (A->sort.sort_key2 < B->sort.sort_key2);
			}
		};

		void sort_by_key() {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		void sort_by_key_range(uint32_t p_from, uint32_t p_size) {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr() + p_from, p_size);
		}

		struct SortByDepth {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->owner->depth < B->owner->depth);
			}
		};

		void sort_by_depth() { //used for shadows

			SortArray<GeometryInstanceSurfaceDataCache *, SortByDepth> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		struct SortByReverseDepthAndPriority {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.priority == B->sort.priority) ? (A->owner->depth > B->owner->depth) : (A->sort.priority < B->sort.priority);
			}
		};

		void sort_by_reverse_depth_and_priority() { //used for alpha

			SortArray<GeometryInstanceSurfaceDataCache *, SortByReverseDepthAndPriority> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		_FORCE_INLINE_ void add_element(GeometryInstanceSurfaceDataCache *p_element) {
			elements.push_back(p_element);
		}
	};

	RenderList render_list[RENDER_LIST_MAX];

	virtual void _update_shader_quality_settings() override;

	/* Effects */

	RendererRD::TAA *taa = nullptr;
	RendererRD::FSR2Effect *fsr2_effect = nullptr;
	RendererRD::DLSSEffect *dlss_effect = nullptr;
	RendererRD::SSEffects *ss_effects = nullptr;
	RendererRD::PlanarReflections *planar_reflections_effect = nullptr;

	// Stand-ins for the DDGI bindings (42-45) when a pass doesn't sample DDGI:
	// zeros, so no volumes.
	RID ddgi_dummy_uniform_buffer;
	RID ddgi_dummy_storage_buffer;

#ifdef METAL_MFXTEMPORAL_ENABLED
	RendererRD::MFXTemporalEffect *mfx_temporal_effect = nullptr;
#endif
	RendererRD::MotionVectorsStore *motion_vectors_store = nullptr;

	/* Cluster builder */

	ClusterBuilderSharedDataRD cluster_builder_shared;
	ClusterBuilderRD *current_cluster_builder = nullptr;

	/* SDFGI */
	void _update_sdfgi(RenderDataRD *p_render_data);

	/* Volumetric fog */
	RID shadow_sampler;

	bool _ensure_volumetric_fog(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment);
	void _update_volumetric_fog(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const Projection &p_cam_projection, const Transform3D &p_cam_transform, const Transform3D &p_prev_cam_inv_transform, RID p_shadow_atlas, int p_directional_light_count, bool p_use_directional_shadows, int p_positional_light_count, int p_voxel_gi_count, const PagedArray<RID> &p_fog_volumes);

	/* Render shadows */

	void _render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier = 0, float p_screen_mesh_lod_threshold = 0.0, bool p_open_pass = true, bool p_close_pass = true, bool p_clear_region = true, RenderingServerTypes::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D());
	void _render_shadow_begin();
	void _render_shadow_append(RID p_framebuffer, const PagedArray<RenderGeometryInstance *> &p_instances, const Projection &p_projection, const Transform3D &p_transform, float p_zfar, float p_bias, float p_normal_bias, bool p_reverse_cull_face, bool p_use_dp, bool p_use_dp_flip, bool p_use_pancake, float p_lod_distance_multiplier = 0.0, float p_screen_mesh_lod_threshold = 0.0, const Rect2i &p_rect = Rect2i(), bool p_flip_y = false, bool p_clear_region = true, bool p_begin = true, bool p_end = true, RenderingServerTypes::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D());
	void _render_shadow_process(RenderBufferDataForwardClustered *p_cull_buffers = nullptr);
	void _render_shadow_end();

	/* Render Scene */
	void _process_ssao(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections);
	void _process_ssil(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections, const Transform3D &p_transform);
	void _process_ssr(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_slices, const Projection *p_projections, const Vector3 *p_eye_offsets, const Transform3D &p_transform);
	void _process_planar_reflections(RenderDataRD *p_render_data, const RID *p_normal_slices);
	void _process_sscs(Ref<RenderSceneBuffersRD> p_render_buffers, const Projection *p_projections, const Transform3D &p_transform, const LocalVector<int> &p_contact_shadows, const RenderShadowData *p_render_shadows, float p_taa_frame_count);
	void _copy_framebuffer_to_ss_effects(Ref<RenderSceneBuffersRD> p_render_buffers, bool p_use_ssil, bool p_use_ssr);
	void _setup_lights_cluster_decals(RenderDataRD *p_render_data, uint32_t &r_directional_light_count, uint32_t &r_positional_light_count);
	void _pre_opaque_render(RenderDataRD *p_render_data, bool p_use_ssao, bool p_use_ssil, bool p_use_ssr, bool p_use_sscs, bool p_use_gi, const RID *p_normal_roughness_slices, const RID *p_voxel_gi_buffer_slices);
	void _process_sss(Ref<RenderSceneBuffersRD> p_render_buffers, const Projection &p_camera);

	/* Debug */
	void _debug_draw_cluster(Ref<RenderSceneBuffersRD> p_render_buffers);

protected:
	/* setup */

	virtual RID _render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override;
	virtual RID _render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override;

	virtual void environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_ssil_quality(RSE::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_ssr_half_size(bool p_half_size) override;
	virtual void environment_set_ssr_roughness_quality(RSE::EnvironmentSSRRoughnessQuality p_quality) override;

	virtual void sub_surface_scattering_set_quality(RSE::SubSurfaceScatteringQuality p_quality) override;
	virtual void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override;

	/* Rendering */

	virtual void _render_scene(RenderDataRD *p_render_data, const Color &p_default_bg_color) override;
	virtual void _render_buffers_debug_draw(const RenderDataRD *p_render_data) override;

	// 3D scaling/upscaling shared between the raster and raytraced render paths.
	enum Scale3DMode {
		SCALE_3D_NONE,
		SCALE_3D_FSR2,
		SCALE_3D_MFX,
		SCALE_3D_DLSS,
	};

	// Optional DLSS Ray Reconstruction guide buffers. Supplied by the caller so
	// the shared upscaler does not depend on the raytracing subsystem (the
	// raytraced path fills these in; the raster path leaves them inactive).
	struct DLSSRRGuideBuffers {
		bool active = false;
		RID diffuse_albedo;
		RID specular_albedo;
		RID normal_roughness;
		RID specular_hit_dist;
	};

	Scale3DMode _resolve_scale_3d_mode(Ref<RenderSceneBuffersRD> p_render_buffers) const;
	void _render_3d_upscaling(const RenderDataRD *p_render_data, Scale3DMode p_scale_type, bool p_using_taa, double p_time_step, const DLSSRRGuideBuffers &p_dlss_rr);
	virtual void _free_rt_viewport_state(RenderSceneBuffersRD *p_render_buffers);

	// Ray traced diffuse GI (DDGI), implemented by RenderForwardClusteredPT.
	// _ddgi_begin_frame() returns true when DDGI provides the indirect light
	// of this view (SDFGI and VoxelGI are then skipped, so it isn't counted
	// twice). _ddgi_process() updates the probes and fills the GI ambient
	// buffer after the depth prepass. _ddgi_debug_draw() runs before upscaling.
	virtual bool _ddgi_begin_frame(RenderDataRD *p_render_data) { return false; }
	virtual void _ddgi_process(RenderDataRD *p_render_data, const RID *p_normal_roughness_slices) {}
	virtual void _ddgi_debug_draw(RenderDataRD *p_render_data) {}
	// The probe resources of this view, for volumetric fog. False when there are none.
	virtual bool _ddgi_get_fog_resources(RenderSceneBuffersRD *p_render_buffers, RID &r_data_buffer, RID &r_probe_buffer, RID &r_irradiance_atlas, RID &r_distance_atlas) { return false; }
	// The probe resources of the last view that applied DDGI, for reflection
	// probe captures. False when there are none.
	virtual bool _ddgi_get_capture_resources(RID &r_data_buffer, RID &r_probe_buffer, RID &r_irradiance_atlas, RID &r_distance_atlas) { return false; }
	// Whether this pass is a reflection probe capture that samples the DDGI
	// probes (rendering/global_illumination/reflections/probe_capture_ddgi), and
	// their resources (data, probes, irradiance, distance) if so.
	bool _probe_capture_ddgi(const RenderDataRD *p_render_data, RID r_resources[4]);
	bool ddgi_active_this_frame = false;

	// Planar reflections selected for the view being rendered (set in _render_scene()).
	bool planar_reflections_active = false;
	int planar_reflection_count = 0;
	const RendererSceneRender::RenderPlanarReflection *planar_reflection_selection[RendererRD::PlanarReflections::MAX_REFLECTORS] = {};

	virtual void _render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region, float p_exposure_normalization) override;
	virtual void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override;
	virtual void _render_sdfgi(Ref<RenderSceneBuffersRD> p_render_buffers, const Vector3i &p_from, const Vector3i &p_size, const AABB &p_bounds, const PagedArray<RenderGeometryInstance *> &p_instances, const RID &p_albedo_texture, const RID &p_emission_texture, const RID &p_emission_aniso_texture, const RID &p_geom_facing_texture, float p_exposure_normalization) override;
	virtual void _render_particle_collider_heightfield(RID p_fb, const Transform3D &p_cam_transform, const Projection &p_cam_projection, const PagedArray<RenderGeometryInstance *> &p_instances) override;

public:
	static RenderForwardClustered *get_singleton() { return singleton; }

	ClusterBuilderSharedDataRD *get_cluster_builder_shared() { return &cluster_builder_shared; }
	RendererRD::SSEffects *get_ss_effects() { return ss_effects; }

	/* callback from updating our lighting UBOs, used to populate cluster builder */
	virtual void setup_added_reflection_probe(const Transform3D &p_transform, const Vector3 &p_half_size) override;
	virtual void setup_added_light(const RSE::LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) override;
	virtual void setup_added_decal(const Transform3D &p_transform, const Vector3 &p_half_size) override;

	virtual void base_uniforms_changed() override;

	/* SDFGI UPDATE */

	virtual void sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) override;
	virtual int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const override;
	virtual AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override;
	virtual uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override;
	RID sdfgi_get_ubo() const { return gi.sdfgi_ubo; }

	/* GEOMETRY INSTANCE */

	virtual RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	virtual void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;

	virtual bool gpu_instances_begin_cull() override;

	virtual uint32_t geometry_instance_get_pair_mask() override;

	/* PIPELINES */

	virtual void mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) override;
	virtual uint32_t get_pipeline_compilations(RSE::PipelineSource p_source) override;

	/* SHADER LIBRARY */

	virtual void enable_features(BitField<FeatureBits> p_feature_bits) override;
	virtual String get_name() const override;

	virtual bool free(RID p_rid) override;

	virtual void update() override;

	RenderForwardClustered();
	~RenderForwardClustered();
};
} // namespace RendererSceneRenderImplementation
