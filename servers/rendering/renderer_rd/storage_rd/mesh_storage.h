/**************************************************************************/
/*  mesh_storage.h                                                        */
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

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid_owner.h"
#include "core/templates/self_list.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_rd/shaders/skeleton.glsl.gen.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/mesh_storage.h"
#include "servers/rendering/storage/utilities.h"

namespace RendererRD {

class MeshStorage : public RendererMeshStorage {
public:
	enum DefaultRDBuffer {
		DEFAULT_RD_BUFFER_VERTEX,
		DEFAULT_RD_BUFFER_NORMAL,
		DEFAULT_RD_BUFFER_TANGENT,
		DEFAULT_RD_BUFFER_COLOR,
		DEFAULT_RD_BUFFER_TEX_UV,
		DEFAULT_RD_BUFFER_TEX_UV2,
		DEFAULT_RD_BUFFER_CUSTOM0,
		DEFAULT_RD_BUFFER_CUSTOM1,
		DEFAULT_RD_BUFFER_CUSTOM2,
		DEFAULT_RD_BUFFER_CUSTOM3,
		DEFAULT_RD_BUFFER_BONES,
		DEFAULT_RD_BUFFER_WEIGHTS,
		DEFAULT_RD_BUFFER_MAX,
	};

	enum IndirectMultiMesh : uint32_t {
		INDIRECT_MULTIMESH_COMMAND_STRIDE = 5
	};

	// Streamed cluster LOD: the cooked indices stay in system memory and each
	// group's index range is uploaded into a shared GPU pool on demand. Roots
	// stay resident, so every frame has a complete cut (see the residency mode
	// of mesh_clod_select.glsl); missing groups only make the cut coarser.
	// With a page file, only the roots' indices stay in memory and the other
	// groups are read from disk on worker threads.
	struct ClusterStreaming {
		uint64_t id = 0; // Unique per setup; reads of a released surface are dropped.
		Vector<uint32_t> indices; // Indices held in memory: all groups, or only the roots with a page file.
		LocalVector<uint32_t> memory_offset; // Per group, into `indices`; UINT32_MAX when only on disk.
		LocalVector<uint32_t> first_index; // Per group, into the cooked index array.
		LocalVector<uint32_t> index_count;
		uint32_t total_indices = 0;
		uint32_t vertex_count = 0;
		String page_file; // Empty when streamed from memory.
		uint64_t page_offset = 0; // Byte offset of this surface's indices in the page file.
		LocalVector<uint8_t> reading; // 1: a disk read is in flight; 2: the read failed.
		LocalVector<uint32_t> first_child; // Per group, into `children`.
		LocalVector<uint32_t> child_count;
		Vector<uint32_t> children;
		LocalVector<int64_t> slot; // Pool offset in indices; -1 when not resident.
		LocalVector<uint64_t> last_used; // Frame of the last feedback that drew the group.
		LocalVector<uint8_t> pinned; // Roots never leave the pool.
		RID residency; // uvec4 per group after a header: flags, first child, pool offset.
		RID children_buffer;
		uint32_t request_offset = 0; // First word of this surface in the shared request buffer.
		bool complete = false; // Every root is resident.
		bool dirty = true; // Residency table needs an upload.
	};

	struct ClusterData {
		RID groups;
		RID index_buffer; // Null when streamed; the pool owns the indices.
		RID index_array;
		uint32_t group_count = 0;
		ClusterStreaming *streaming = nullptr;
	};

private:
	static MeshStorage *singleton;

	RID default_rd_storage_buffer;

	/* Mesh */

	RID mesh_default_rd_buffers[DEFAULT_RD_BUFFER_MAX];

	struct MeshInstance;

	struct Mesh {
		struct Surface {
			RSE::PrimitiveType primitive = RSE::PRIMITIVE_POINTS;
			uint64_t format = 0;

			uint32_t vertex_count = 0;
			RID vertex_buffer;
			uint32_t vertex_buffer_size = 0;

			RID attribute_buffer;
			uint32_t attribute_buffer_size = 0;

			RID skin_buffer;
			uint32_t skin_buffer_size = 0;

			// A different pipeline needs to be allocated
			// depending on the inputs available in the
			// material.
			// There are never that many geometry/material
			// combinations, so a simple array is the most
			// cache-efficient structure.

			struct Version {
				uint64_t input_mask = 0;
				uint32_t current_buffer = 0;
				uint32_t previous_buffer = 0;
				bool input_motion_vectors = false;
				bool point_size_emulated = false;
				RD::VertexFormatID vertex_format = 0;
				RID vertex_array;
			};

			SpinLock version_lock; //needed to access versions
			Version *versions = nullptr; //allocated on demand
			uint32_t version_count = 0;

			RID index_buffer;
			uint32_t index_buffer_size = 0;
			RID index_array;
			uint32_t index_count = 0;
			ClusterData cluster_data;

			struct LOD {
				float edge_length = 0.0;
				uint32_t index_count = 0;
				RID index_buffer;
				uint32_t index_buffer_size = 0;
				RID index_array;
			};

			LOD *lods = nullptr;
			uint32_t lod_count = 0;

			AABB aabb;

			Vector<AABB> bone_aabbs;

			// Transform used in runtime bone AABBs compute.
			// As bone AABBs are saved in Mesh space, but bones animation is in Skeleton space.
			Transform3D mesh_to_skeleton_xform;

			Vector4 uv_scale;

			RID blend_shape_buffer;
			uint32_t blend_shape_buffer_size = 0;

			RID material;

			uint32_t rt_invalidation_counter = 0; // Bump on geometry changes for RT cache invalidation.

			uint32_t render_index = 0;
			uint64_t render_pass = 0;

			uint32_t multimesh_render_index = 0;
			uint64_t multimesh_render_pass = 0;

			uint32_t particles_render_index = 0;
			uint64_t particles_render_pass = 0;

			RID uniform_set;
		};

		uint32_t blend_shape_count = 0;
		RSE::BlendShapeMode blend_shape_mode = RSE::BLEND_SHAPE_MODE_NORMALIZED;

		Surface **surfaces = nullptr;
		uint32_t surface_count = 0;

		bool has_bone_weights = false;

		AABB aabb;
		AABB custom_aabb;
		uint64_t skeleton_aabb_version = 0;
		RID skeleton_aabb_rid;

		Vector<RID> material_cache;

		List<MeshInstance *> instances;

		RID shadow_mesh;
		HashSet<Mesh *> shadow_owners;

		String path;

		Dependency dependency;
	};

	mutable RID_Owner<Mesh, true> mesh_owner;

	/* Mesh Instance API */

	struct MeshInstance {
		Mesh *mesh = nullptr;
		RID skeleton;
		struct Surface {
			RID vertex_buffer[2];
			RID uniform_set[2];
			uint32_t current_buffer = 0;
			uint32_t previous_buffer = 0;
			uint64_t last_change = 0;

			Mesh::Surface::Version *versions = nullptr; //allocated on demand
			uint32_t version_count = 0;
		};
		LocalVector<Surface> surfaces;
		LocalVector<float> blend_weights;

		RID blend_weights_buffer;
		List<MeshInstance *>::Element *I = nullptr; //used to erase itself
		uint64_t skeleton_version = 0;
		bool dirty = false;
		bool weights_dirty = false;
		SelfList<MeshInstance> weight_update_list;
		SelfList<MeshInstance> array_update_list;
		Transform2D canvas_item_transform_2d;
		MeshInstance() :
				weight_update_list(this), array_update_list(this) {}
	};

	RD::VertexFormatID _mesh_surface_generate_vertex_format(uint64_t p_surface_format, uint64_t p_input_mask, bool p_instanced_surface, bool p_input_motion_vectors, bool p_point_size_emulated, uint32_t &r_position_stride);
	void _mesh_surface_generate_version_for_input_mask(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, bool p_input_motion_vectors, bool p_point_size_emulated, MeshInstance::Surface *mis = nullptr, uint32_t p_current_buffer = 0, uint32_t p_previous_buffer = 0);
	void _mesh_surface_clear(Mesh *p_mesh, int p_surface);
	void _mesh_cluster_clear(Mesh::Surface *p_surface);

	// Shared GPU pool for streamed cluster indices, and the GPU request buffer
	// that the selection kernel fills (request and used bits per group).
	struct ClusterStreamingPool {
		struct Range {
			uint32_t offset = 0;
			uint32_t size = 0;
		};
		RID index_buffer;
		RID index_array;
		uint32_t capacity = 0; // In indices.
		uint64_t budget_bytes = 0;
		LocalVector<Range> free_ranges; // Sorted by offset, coalesced.
		LocalVector<Mesh::Surface *> surfaces;
		RID requests;
		uint32_t request_capacity = 0;
		uint32_t request_words = 0;
		bool layout_dirty = true;
		uint64_t layout_version = 0;
		uint64_t frame = UINT64_MAX;
		bool readback_pending = false;
		Vector<uint32_t> feedback;
		uint64_t feedback_version = 0;
		uint64_t feedback_frame = 0;
		bool feedback_ready = false;
		uint64_t resident_indices = 0;
		uint64_t loads = 0;
		uint64_t evictions = 0;
		uint64_t deferred = 0; // Requests left for later frames (upload limit or full pool).
		// Disk reads: one batch per page file and frame, filled by a worker task
		// and consumed on the render thread once the task has completed.
		struct PageRead {
			uint64_t surface_id = 0;
			uint32_t group = 0;
			uint64_t file_offset = 0;
			uint32_t count = 0;
			uint32_t vertex_count = 0;
			uint32_t data_offset = 0; // Into the batch's `data`.
			bool ok = false;
		};
		struct PageBatch {
			String path;
			LocalVector<PageRead> reads;
			Vector<uint32_t> data;
			WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		};
		LocalVector<PageBatch *> page_batches;
		HashMap<uint64_t, Mesh::Surface *> surface_ids;
		uint64_t next_surface_id = 1;
		uint32_t reading_indices = 0; // In flight across all batches.
		uint64_t disk_reads = 0;
		uint64_t disk_bytes = 0;
		uint64_t disk_errors = 0;
	} cluster_pool;

	bool _cluster_pool_create(uint64_t p_budget_bytes);
	bool _cluster_pool_allocate(uint32_t p_size, uint32_t &r_offset);
	void _cluster_pool_free(uint32_t p_offset, uint32_t p_size);
	bool _cluster_pool_evict(uint32_t p_size, uint64_t p_before_frame);
	bool _cluster_streaming_store(Mesh::Surface *p_surface, uint32_t p_group, const uint32_t *p_indices);
	bool _cluster_streaming_load(Mesh::Surface *p_surface, uint32_t p_group);
	uint64_t _cluster_streaming_keep_after() const;
	static bool _cluster_page_file_check(const String &p_path, int p_surface, const PackedByteArray &p_hash, uint32_t p_index_count, uint64_t &r_offset);
	static void _cluster_page_batch_read(void *p_batch);
	static bool _cluster_page_file_read_all(const Dictionary &p_record, PackedInt32Array &r_indices);
	void _cluster_page_batches_finish(bool p_wait);
	void _cluster_streaming_unload(Mesh::Surface *p_surface, uint32_t p_group);
	void _cluster_streaming_upload_table(Mesh::Surface *p_surface);
	bool _cluster_streaming_setup(Mesh::Surface *p_surface, const Dictionary &p_record);
	void _cluster_streaming_release(Mesh::Surface *p_surface);
	void _cluster_streaming_apply_feedback();
	static void _cluster_streaming_feedback(const Vector<uint8_t> &p_data, uint64_t p_version, uint64_t p_frame);

	void _mesh_instance_clear(MeshInstance *mi);
	void _mesh_instance_add_surface(MeshInstance *mi, Mesh *mesh, uint32_t p_surface);
	void _mesh_instance_add_surface_buffer(MeshInstance *mi, Mesh *mesh, MeshInstance::Surface *s, uint32_t p_surface, uint32_t p_buffer_index);
	void _mesh_instance_remove_surface(MeshInstance *mi, int p_surface);

	mutable RID_Owner<MeshInstance> mesh_instance_owner;

	SelfList<MeshInstance>::List dirty_mesh_instance_weights;
	SelfList<MeshInstance>::List dirty_mesh_instance_arrays;

	/* MultiMesh */

	struct MultiMesh {
		RID mesh;
		int instances = 0;
		float thinning_begin = 0.0f; // Distance thinning (world units); end 0 = off.
		float thinning_end = 0.0f;
		RSE::MultimeshTransformFormat xform_format = RSE::MULTIMESH_TRANSFORM_3D;
		bool uses_colors = false;
		bool uses_custom_data = false;
		int visible_instances = -1;
		AABB aabb;
		AABB custom_aabb;
		bool aabb_dirty = false;
		bool buffer_set = false;
		bool indirect = false;
		bool motion_vectors_enabled = false;
		uint32_t motion_vectors_current_offset = 0;
		uint32_t motion_vectors_previous_offset = 0;
		uint64_t motion_vectors_last_change = -1;
		uint32_t stride_cache = 0;
		uint32_t color_offset_cache = 0;
		uint32_t custom_data_offset_cache = 0;

		Vector<float> data_cache; //used if individual setting is used
		bool *data_cache_dirty_regions = nullptr;
		uint32_t data_cache_dirty_region_count = 0;
		bool *previous_data_cache_dirty_regions = nullptr;
		uint32_t previous_data_cache_dirty_region_count = 0;

		RID buffer; //storage buffer
		RID uniform_set_3d;
		RID uniform_set_2d;
		RID command_buffer; //used if indirect setting is used

		bool dirty = false;
		MultiMesh *dirty_list = nullptr;

		RendererMeshStorage::MultiMeshInterpolator interpolator;

		Dependency dependency;
	};

	mutable RID_Owner<MultiMesh, true> multimesh_owner;

	MultiMesh *multimesh_dirty_list = nullptr;

	void _multimesh_make_local(MultiMesh *multimesh) const;
	void _multimesh_enable_motion_vectors(MultiMesh *multimesh);
	void _multimesh_update_motion_vectors_data_cache(MultiMesh *multimesh);
	bool _multimesh_uses_motion_vectors(MultiMesh *multimesh);
	void _multimesh_mark_dirty(MultiMesh *multimesh, int p_index, bool p_aabb);
	void _multimesh_mark_all_dirty(MultiMesh *multimesh, bool p_data, bool p_aabb);
	void _multimesh_re_create_aabb(MultiMesh *multimesh, const float *p_data, int p_instances);
	void _multimesh_create_indirect_command_buffer(MultiMesh *multimesh);

	/* Skeleton */

	struct SkeletonShader {
		struct PushConstant {
			uint32_t has_normal;
			uint32_t has_tangent;
			uint32_t has_skeleton;
			uint32_t has_blend_shape;

			uint32_t vertex_count;
			uint32_t vertex_stride;
			uint32_t skin_stride;
			uint32_t skin_weight_offset;

			uint32_t blend_shape_count;
			uint32_t normalized_blend_shapes;
			uint32_t normal_tangent_stride;
			uint32_t pad1;
			float skeleton_transform_x[2];
			float skeleton_transform_y[2];

			float skeleton_transform_offset[2];
			float inverse_transform_x[2];

			float inverse_transform_y[2];
			float inverse_transform_offset[2];
		};

		enum {
			UNIFORM_SET_INSTANCE = 0,
			UNIFORM_SET_SURFACE = 1,
			UNIFORM_SET_SKELETON = 2,
		};
		enum {
			SHADER_MODE_2D,
			SHADER_MODE_3D,
			SHADER_MODE_MAX
		};

		SkeletonShaderRD shader;
		RID version;
		RID version_shader[SHADER_MODE_MAX];
		RID pipeline[SHADER_MODE_MAX];

		RID default_skeleton_uniform_set;
	} skeleton_shader;

	struct Skeleton {
		bool use_2d = false;
		int size = 0;
		LocalVector<float> data;
		RID buffer;

		bool dirty = false;
		Skeleton *dirty_list = nullptr;
		Transform2D base_transform_2d;

		RID uniform_set_3d;
		RID uniform_set_mi;

		uint64_t version = 1;

		Dependency dependency;
	};

	mutable RID_Owner<Skeleton, true> skeleton_owner;

	_FORCE_INLINE_ void _skeleton_make_dirty(Skeleton *skeleton);

	Skeleton *skeleton_dirty_list = nullptr;

	enum AttributeLocation {
		ATTRIBUTE_LOCATION_PREV_VERTEX = 12,
		ATTRIBUTE_LOCATION_PREV_NORMAL = 13,
		ATTRIBUTE_LOCATION_PREV_TANGENT = 14
	};

public:
	static MeshStorage *get_singleton();

	/* Cluster LOD streaming */

	// Applies last frames' GPU feedback (loads, evictions, residency tables),
	// then copies and clears the request buffer. Once per frame, before selection.
	void cluster_streaming_begin_frame();
	RID cluster_streaming_get_requests() const { return cluster_pool.requests; }
	Dictionary cluster_streaming_get_statistics() const;

	MeshStorage();
	virtual ~MeshStorage();

	bool free(RID p_rid);

	RID get_default_rd_storage_buffer() const { return default_rd_storage_buffer; }

	/* MESH API */

	bool owns_mesh(RID p_rid) { return mesh_owner.owns(p_rid); }

	virtual RID mesh_allocate() override;
	virtual void mesh_initialize(RID p_mesh) override;
	virtual void mesh_free(RID p_rid) override;

	virtual void mesh_set_blend_shape_count(RID p_mesh, int p_blend_shape_count) override;

	/// Return stride
	virtual void mesh_add_surface(RID p_mesh, const RenderingServerTypes::SurfaceData &p_surface) override;

	virtual int mesh_get_blend_shape_count(RID p_mesh) const override;

	virtual void mesh_set_blend_shape_mode(RID p_mesh, RSE::BlendShapeMode p_mode) override;
	virtual RSE::BlendShapeMode mesh_get_blend_shape_mode(RID p_mesh) const override;

	virtual void mesh_surface_update_vertex_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;
	virtual void mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;
	virtual void mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;
	virtual void mesh_surface_update_index_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) override;

	virtual void mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) override;
	virtual RID mesh_surface_get_material(RID p_mesh, int p_surface) const override;

	virtual RenderingServerTypes::SurfaceData mesh_get_surface(RID p_mesh, int p_surface) const override;

	virtual RID mesh_surface_get_vertex_buffer_rd_rid(RID p_mesh, int p_surface) const override;
	virtual RID mesh_surface_get_attribute_buffer_rd_rid(RID p_mesh, int p_surface) const override;
	virtual RID mesh_surface_get_skin_buffer_rd_rid(RID p_mesh, int p_surface) const override;
	virtual RID mesh_surface_get_index_buffer_rd_rid(RID p_mesh, int p_surface) const override;

	virtual int mesh_get_surface_count(RID p_mesh) const override;

	virtual void mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) override;
	virtual AABB mesh_get_custom_aabb(RID p_mesh) const override;

	virtual AABB mesh_get_aabb(RID p_mesh, RID p_skeleton = RID()) override;
	virtual void mesh_set_shadow_mesh(RID p_mesh, RID p_shadow_mesh) override;
	virtual void mesh_set_cluster_data(RID p_mesh, const Array &p_data) override;
	_FORCE_INLINE_ const ClusterData &mesh_surface_get_cluster_data(void *p_surface) const {
		return static_cast<Mesh::Surface *>(p_surface)->cluster_data;
	}

	virtual void mesh_set_path(RID p_mesh, const String &p_path) override;
	virtual String mesh_get_path(RID p_mesh) const override;

	virtual void mesh_clear(RID p_mesh) override;
	virtual void mesh_surface_remove(RID p_mesh, int p_surface) override;

	virtual void mesh_debug_usage(List<RenderingServerTypes::MeshInfo> *r_info) override;

	virtual bool mesh_needs_instance(RID p_mesh, bool p_has_skeleton) override;

	_FORCE_INLINE_ const RID *mesh_get_surface_count_and_materials(RID p_mesh, uint32_t &r_surface_count) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		ERR_FAIL_NULL_V(mesh, nullptr);
		r_surface_count = mesh->surface_count;
		if (r_surface_count == 0) {
			return nullptr;
		}
		if (mesh->material_cache.is_empty()) {
			mesh->material_cache.resize(mesh->surface_count);
			for (uint32_t i = 0; i < r_surface_count; i++) {
				mesh->material_cache.write[i] = mesh->surfaces[i]->material;
			}
		}

		return mesh->material_cache.ptr();
	}

	_FORCE_INLINE_ void *mesh_get_surface(RID p_mesh, uint32_t p_surface_index) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		ERR_FAIL_NULL_V(mesh, nullptr);
		ERR_FAIL_UNSIGNED_INDEX_V(p_surface_index, mesh->surface_count, nullptr);

		return mesh->surfaces[p_surface_index];
	}

	_FORCE_INLINE_ RID mesh_get_shadow_mesh(RID p_mesh) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		ERR_FAIL_NULL_V(mesh, RID());

		return mesh->shadow_mesh;
	}

	_FORCE_INLINE_ RSE::PrimitiveType mesh_surface_get_primitive(void *p_surface) {
		Mesh::Surface *surface = reinterpret_cast<Mesh::Surface *>(p_surface);
		return surface->primitive;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_vertex_count(void *p_surface) {
		Mesh::Surface *surface = reinterpret_cast<Mesh::Surface *>(p_surface);
		return surface->vertex_count;
	}

	_FORCE_INLINE_ bool mesh_surface_has_lod(void *p_surface) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->lod_count > 0;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_lod_count(void *p_surface) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->lod_count;
	}

	/// Screen-space edge length of index LOD p_lod (0 = first reduced LOD), as used by mesh_surface_get_lod().
	_FORCE_INLINE_ float mesh_surface_get_lod_edge_length(void *p_surface, uint32_t p_lod) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		ERR_FAIL_UNSIGNED_INDEX_V(p_lod, s->lod_count, 0.0f);
		return s->lods[p_lod].edge_length;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_vertices_drawn_count(void *p_surface) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->index_count ? s->index_count : s->vertex_count;
	}

	_FORCE_INLINE_ AABB mesh_surface_get_aabb(void *p_surface) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->aabb;
	}

	_FORCE_INLINE_ uint64_t mesh_surface_get_format(void *p_surface) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->format;
	}

	_FORCE_INLINE_ Vector4 mesh_surface_get_uv_scale(void *p_surface) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->uv_scale;
	}

	/// Get the vertex buffer RID for raytracing device address access.
	_FORCE_INLINE_ RID mesh_surface_get_vertex_buffer(void *p_surface) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->vertex_buffer;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_position_stride(void *p_surface) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		if (s->format & RSE::ARRAY_FLAG_USE_2D_VERTICES) {
			return sizeof(float) * 2;
		} else if (s->format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
			return sizeof(uint16_t) * 4;
		} else {
			return sizeof(float) * 3;
		}
	}

	/// Get the per-mesh-instance skinned/blend-shape vertex buffer for the current frame.
	/// Returns RID() when this surface is not deformed (caller should fall back to mesh_surface_get_vertex_buffer).
	_FORCE_INLINE_ RID mesh_instance_get_vertex_buffer(RID p_mesh_instance, uint32_t p_surface_index) {
		MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
		if (!mi || p_surface_index >= mi->surfaces.size()) {
			return RID();
		}
		const MeshInstance::Surface &mis = mi->surfaces[p_surface_index];
		return mis.vertex_buffer[mis.current_buffer];
	}

	/// Get the previous-frame skinned vertex buffer (for motion vector reconstruction).
	/// Returns the current buffer when the surface was not updated this frame, or when
	/// update_mesh_instances() did not double-buffer this surface.
	_FORCE_INLINE_ RID mesh_instance_get_prev_vertex_buffer(RID p_mesh_instance, uint32_t p_surface_index) {
		MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
		if (!mi || p_surface_index >= mi->surfaces.size()) {
			return RID();
		}
		const MeshInstance::Surface &mis = mi->surfaces[p_surface_index];
		uint32_t previous_buffer = RSG::rasterizer->get_frame_number() == mis.last_change ? mis.previous_buffer : mis.current_buffer;
		return mis.vertex_buffer[previous_buffer];
	}

	/// Frame number on which this mesh-instance surface was last skinned (0 if never deformed).
	_FORCE_INLINE_ uint64_t mesh_instance_get_last_change(RID p_mesh_instance, uint32_t p_surface_index) {
		MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
		if (!mi || p_surface_index >= mi->surfaces.size()) {
			return 0;
		}
		return mi->surfaces[p_surface_index].last_change;
	}

	/// Get the attribute buffer RID for raytracing device address access.
	_FORCE_INLINE_ RID mesh_surface_get_attribute_buffer(void *p_surface) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->attribute_buffer;
	}

	/// Get the index buffer RID for raytracing device address access.
	/// @param p_lod LOD level (0 = base, 1+ = LOD index + 1)
	_FORCE_INLINE_ RID mesh_surface_get_index_buffer(void *p_surface, uint32_t p_lod = 0) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		if (p_lod == 0) {
			return s->index_buffer;
		} else {
			ERR_FAIL_UNSIGNED_INDEX_V(p_lod - 1, s->lod_count, RID());
			return s->lods[p_lod - 1].index_buffer;
		}
	}

	/// Get the index count for raytracing.
	/// @param p_lod LOD level (0 = base, 1+ = LOD index + 1)
	_FORCE_INLINE_ uint32_t mesh_surface_get_index_count(void *p_surface, uint32_t p_lod = 0) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		if (p_lod == 0) {
			return s->index_count;
		} else {
			ERR_FAIL_UNSIGNED_INDEX_V(p_lod - 1, s->lod_count, 0);
			return s->lods[p_lod - 1].index_count;
		}
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_lod(void *p_surface, float p_model_scale, float p_distance_threshold, float p_mesh_lod_threshold, uint32_t &r_index_count) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);

		int32_t current_lod = -1;
		r_index_count = s->index_count;
		for (uint32_t i = 0; i < s->lod_count; i++) {
			float screen_size = s->lods[i].edge_length * p_model_scale / p_distance_threshold;
			if (screen_size > p_mesh_lod_threshold) {
				break;
			}
			current_lod = i;
		}
		if (current_lod == -1) {
			return 0;
		} else {
			r_index_count = s->lods[current_lod].index_count;
			return current_lod + 1;
		}
	}

	_FORCE_INLINE_ RID mesh_surface_get_index_array(void *p_surface, uint32_t p_lod) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);

		if (p_lod == 0) {
			return s->index_array;
		} else {
			return s->lods[p_lod - 1].index_array;
		}
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_rt_invalidation_counter(void *p_surface) const {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		return s->rt_invalidation_counter;
	}

	_FORCE_INLINE_ void mesh_surface_get_vertex_arrays_and_format(void *p_surface, uint64_t p_input_mask, bool p_input_motion_vectors, bool p_point_size_emulated, RID &r_vertex_array_rd, RD::VertexFormatID &r_vertex_format) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);

		s->version_lock.lock();

		//there will never be more than, at much, 3 or 4 versions, so iterating is the fastest way

		for (uint32_t i = 0; i < s->version_count; i++) {
			if (s->versions[i].input_mask != p_input_mask || s->versions[i].input_motion_vectors != p_input_motion_vectors || s->versions[i].point_size_emulated != p_point_size_emulated) {
				// Find the version that matches the inputs required.
				continue;
			}

			//we have this version, hooray
			r_vertex_format = s->versions[i].vertex_format;
			r_vertex_array_rd = s->versions[i].vertex_array;
			s->version_lock.unlock();
			return;
		}

		uint32_t version = s->version_count;
		s->version_count++;
		s->versions = (Mesh::Surface::Version *)memrealloc(s->versions, sizeof(Mesh::Surface::Version) * s->version_count);

		_mesh_surface_generate_version_for_input_mask(s->versions[version], s, p_input_mask, p_input_motion_vectors, p_point_size_emulated);

		r_vertex_format = s->versions[version].vertex_format;
		r_vertex_array_rd = s->versions[version].vertex_array;

		s->version_lock.unlock();
	}

	_FORCE_INLINE_ void mesh_instance_surface_get_vertex_arrays_and_format(RID p_mesh_instance, uint64_t p_surface_index, uint64_t p_input_mask, bool p_input_motion_vectors, bool p_point_size_emulated, RID &r_vertex_array_rd, RD::VertexFormatID &r_vertex_format) {
		MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
		ERR_FAIL_NULL(mi);
		Mesh *mesh = mi->mesh;
		ERR_FAIL_UNSIGNED_INDEX(p_surface_index, mesh->surface_count);

		MeshInstance::Surface *mis = &mi->surfaces[p_surface_index];
		Mesh::Surface *s = mesh->surfaces[p_surface_index];
		uint32_t current_buffer = mis->current_buffer;

		// Using the previous buffer is only allowed if the surface was updated this frame and motion vectors are required.
		uint32_t previous_buffer = p_input_motion_vectors && (RSG::rasterizer->get_frame_number() == mis->last_change) ? mis->previous_buffer : current_buffer;

		s->version_lock.lock();

		//there will never be more than, at much, 3 or 4 versions, so iterating is the fastest way

		for (uint32_t i = 0; i < mis->version_count; i++) {
			if (mis->versions[i].input_mask != p_input_mask || mis->versions[i].input_motion_vectors != p_input_motion_vectors) {
				// Find the version that matches the inputs required.
				continue;
			}

			if (mis->versions[i].current_buffer != current_buffer || mis->versions[i].previous_buffer != previous_buffer) {
				// Find the version that corresponds to the correct buffers that should be used.
				continue;
			}

			//we have this version, hooray
			r_vertex_format = mis->versions[i].vertex_format;
			r_vertex_array_rd = mis->versions[i].vertex_array;
			s->version_lock.unlock();
			return;
		}

		uint32_t version = mis->version_count;
		mis->version_count++;
		mis->versions = (Mesh::Surface::Version *)memrealloc(mis->versions, sizeof(Mesh::Surface::Version) * mis->version_count);

		_mesh_surface_generate_version_for_input_mask(mis->versions[version], s, p_input_mask, p_input_motion_vectors, p_point_size_emulated, mis, current_buffer, previous_buffer);

		r_vertex_format = mis->versions[version].vertex_format;
		r_vertex_array_rd = mis->versions[version].vertex_array;

		s->version_lock.unlock();
	}

	_FORCE_INLINE_ RID mesh_get_default_rd_buffer(DefaultRDBuffer p_buffer) {
		ERR_FAIL_INDEX_V(p_buffer, DEFAULT_RD_BUFFER_MAX, RID());
		return mesh_default_rd_buffers[p_buffer];
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_render_pass_index(RID p_mesh, uint32_t p_surface_index, uint64_t p_render_pass, uint32_t *r_index) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		Mesh::Surface *s = mesh->surfaces[p_surface_index];

		if (s->render_pass != p_render_pass) {
			(*r_index)++;
			s->render_pass = p_render_pass;
			s->render_index = *r_index;
		}

		return s->render_index;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_multimesh_render_pass_index(RID p_mesh, uint32_t p_surface_index, uint64_t p_render_pass, uint32_t *r_index) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		Mesh::Surface *s = mesh->surfaces[p_surface_index];

		if (s->multimesh_render_pass != p_render_pass) {
			(*r_index)++;
			s->multimesh_render_pass = p_render_pass;
			s->multimesh_render_index = *r_index;
		}

		return s->multimesh_render_index;
	}

	_FORCE_INLINE_ uint32_t mesh_surface_get_particles_render_pass_index(RID p_mesh, uint32_t p_surface_index, uint64_t p_render_pass, uint32_t *r_index) {
		Mesh *mesh = mesh_owner.get_or_null(p_mesh);
		Mesh::Surface *s = mesh->surfaces[p_surface_index];

		if (s->particles_render_pass != p_render_pass) {
			(*r_index)++;
			s->particles_render_pass = p_render_pass;
			s->particles_render_index = *r_index;
		}

		return s->particles_render_index;
	}

	_FORCE_INLINE_ RD::VertexFormatID mesh_surface_get_vertex_format(void *p_surface, uint64_t p_input_mask, bool p_instanced_surface, bool p_input_motion_vectors, bool p_point_size_emulated) {
		Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
		uint32_t position_stride = 0;
		return _mesh_surface_generate_vertex_format(s->format, p_input_mask, p_instanced_surface, p_input_motion_vectors, p_point_size_emulated, position_stride);
	}

	Dependency *mesh_get_dependency(RID p_mesh) const;

	/* MESH INSTANCE API */

	bool owns_mesh_instance(RID p_rid) const { return mesh_instance_owner.owns(p_rid); }

	virtual RID mesh_instance_create(RID p_base) override;
	virtual void mesh_instance_free(RID p_rid) override;
	virtual void mesh_instance_set_skeleton(RID p_mesh_instance, RID p_skeleton) override;
	virtual void mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) override;
	virtual void mesh_instance_check_for_update(RID p_mesh_instance) override;
	virtual void mesh_instance_set_canvas_item_transform(RID p_mesh_instance, const Transform2D &p_transform) override;
	virtual void update_mesh_instances() override;

	/* MULTIMESH API */

	bool owns_multimesh(RID p_rid) { return multimesh_owner.owns(p_rid); }

	virtual RID _multimesh_allocate() override;
	virtual void _multimesh_initialize(RID p_multimesh) override;
	virtual void _multimesh_free(RID p_rid) override;

	virtual void _multimesh_allocate_data(RID p_multimesh, int p_instances, RSE::MultimeshTransformFormat p_transform_format, bool p_use_colors = false, bool p_use_custom_data = false, bool p_use_indirect = false) override;
	virtual int _multimesh_get_instance_count(RID p_multimesh) const override;

	virtual void _multimesh_set_mesh(RID p_multimesh, RID p_mesh) override;
	virtual void _multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) override;
	virtual void _multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) override;
	virtual void _multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) override;
	virtual void _multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) override;

	virtual RID _multimesh_get_mesh(RID p_multimesh) const override;

	virtual Transform3D _multimesh_instance_get_transform(RID p_multimesh, int p_index) const override;
	virtual Transform2D _multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const override;
	virtual Color _multimesh_instance_get_color(RID p_multimesh, int p_index) const override;
	virtual Color _multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const override;

	virtual void _multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) override;
	virtual RID _multimesh_get_command_buffer_rd_rid(RID p_multimesh) const override;
	virtual RID _multimesh_get_buffer_rd_rid(RID p_multimesh) const override;
	virtual Vector<float> _multimesh_get_buffer(RID p_multimesh) const override;

	virtual void _multimesh_set_visible_instances(RID p_multimesh, int p_visible) override;
	virtual void multimesh_set_distance_thinning(RID p_multimesh, float p_begin, float p_end) override {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL(multimesh);
		ERR_FAIL_COND(!Math::is_finite(p_begin) || !Math::is_finite(p_end) || p_begin < 0.0f || (p_end > 0.0f && p_end <= p_begin));
		multimesh->thinning_begin = p_begin;
		multimesh->thinning_end = p_end;
	}
	_FORCE_INLINE_ void multimesh_get_distance_thinning(RID p_multimesh, float &r_begin, float &r_end) const {
		const MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		r_begin = multimesh ? multimesh->thinning_begin : 0.0f;
		r_end = multimesh ? multimesh->thinning_end : 0.0f;
	}
	virtual int _multimesh_get_visible_instances(RID p_multimesh) const override;

	virtual void _multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) override;
	virtual AABB _multimesh_get_custom_aabb(RID p_multimesh) const override;

	virtual AABB _multimesh_get_aabb(RID p_multimesh) override;

	virtual MultiMeshInterpolator *_multimesh_get_interpolator(RID p_multimesh) const override;

	void _update_dirty_multimeshes();
	void _multimesh_get_motion_vectors_offsets(RID p_multimesh, uint32_t &r_current_offset, uint32_t &r_prev_offset);
	bool _multimesh_uses_motion_vectors_offsets(RID p_multimesh);
	bool _multimesh_uses_motion_vectors(RID p_multimesh);

	_FORCE_INLINE_ bool multimesh_uses_indirect(RID p_multimesh) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL_V(multimesh, false);
		return multimesh->indirect;
	}

	_FORCE_INLINE_ RSE::MultimeshTransformFormat multimesh_get_transform_format(RID p_multimesh) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL_V(multimesh, RSE::MULTIMESH_TRANSFORM_3D);
		return multimesh->xform_format;
	}

	_FORCE_INLINE_ bool multimesh_uses_colors(RID p_multimesh) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL_V(multimesh, false);
		return multimesh->uses_colors;
	}

	_FORCE_INLINE_ bool multimesh_uses_custom_data(RID p_multimesh) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL_V(multimesh, false);
		return multimesh->uses_custom_data;
	}

	_FORCE_INLINE_ uint32_t multimesh_get_instances_to_draw(RID p_multimesh) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		ERR_FAIL_NULL_V(multimesh, 0);
		if (multimesh->visible_instances >= 0) {
			return multimesh->visible_instances;
		}
		return multimesh->instances;
	}

	// Returns a pointer to the CPU-side transform buffer, syncing from GPU once if needed.
	// Safe to call every frame; the readback only happens on the first call when data_cache is empty.
	// Note: for MultiMeshes updated every frame via set_buffer(), data_cache may be stale after init.
	_FORCE_INLINE_ const float *multimesh_get_local_data_ptr(RID p_multimesh) const {
		MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
		if (!mm) {
			return nullptr;
		}
		_multimesh_make_local(mm);
		return mm->data_cache.is_empty() ? nullptr : mm->data_cache.ptr();
	}

	_FORCE_INLINE_ uint32_t multimesh_get_stride(RID p_multimesh) const {
		MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
		return mm ? mm->stride_cache : 0;
	}

	_FORCE_INLINE_ uint32_t multimesh_get_current_instance_offset(RID p_multimesh) const {
		MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
		return mm ? mm->motion_vectors_current_offset : 0;
	}

	_FORCE_INLINE_ uint64_t multimesh_get_last_change(RID p_multimesh) const {
		MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
		return mm ? mm->motion_vectors_last_change : (uint64_t)-1;
	}

	_FORCE_INLINE_ RID multimesh_get_gpu_buffer(RID p_multimesh) const {
		MultiMesh *mm = multimesh_owner.get_or_null(p_multimesh);
		return mm ? mm->buffer : RID();
	}

	_FORCE_INLINE_ RID multimesh_get_3d_uniform_set(RID p_multimesh, RID p_shader, uint32_t p_set) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		if (multimesh == nullptr) {
			return RID();
		}
		if (!multimesh->uniform_set_3d.is_valid()) {
			if (!multimesh->buffer.is_valid()) {
				return RID();
			}
			Vector<RD::Uniform> uniforms;
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = 0;
			u.append_id(multimesh->buffer);
			uniforms.push_back(u);
			multimesh->uniform_set_3d = RD::get_singleton()->uniform_set_create(uniforms, p_shader, p_set);
		}

		return multimesh->uniform_set_3d;
	}

	_FORCE_INLINE_ RID multimesh_get_2d_uniform_set(RID p_multimesh, RID p_shader, uint32_t p_set) const {
		MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
		if (multimesh == nullptr) {
			return RID();
		}
		if (!multimesh->uniform_set_2d.is_valid()) {
			if (!multimesh->buffer.is_valid()) {
				return RID();
			}
			Vector<RD::Uniform> uniforms;
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = 0;
			u.append_id(multimesh->buffer);
			uniforms.push_back(u);
			multimesh->uniform_set_2d = RD::get_singleton()->uniform_set_create(uniforms, p_shader, p_set);
		}

		return multimesh->uniform_set_2d;
	}

	Dependency *multimesh_get_dependency(RID p_multimesh) const;

	/* SKELETON API */

	bool owns_skeleton(RID p_rid) const { return skeleton_owner.owns(p_rid); }

	virtual RID skeleton_allocate() override;
	virtual void skeleton_initialize(RID p_skeleton) override;
	virtual void skeleton_free(RID p_rid) override;

	virtual void skeleton_allocate_data(RID p_skeleton, int p_bones, bool p_2d_skeleton = false) override;
	virtual void skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) override;
	virtual int skeleton_get_bone_count(RID p_skeleton) const override;
	virtual void skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) override;
	virtual Transform3D skeleton_bone_get_transform(RID p_skeleton, int p_bone) const override;
	virtual void skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) override;
	virtual Transform2D skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const override;

	virtual void skeleton_update_dependency(RID p_skeleton, DependencyTracker *p_instance) override;

	void _update_dirty_skeletons();

	_FORCE_INLINE_ bool skeleton_is_valid(RID p_skeleton) {
		return skeleton_owner.get_or_null(p_skeleton) != nullptr;
	}

	_FORCE_INLINE_ RID skeleton_get_3d_uniform_set(RID p_skeleton, RID p_shader, uint32_t p_set) const {
		Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);
		ERR_FAIL_NULL_V(skeleton, RID());
		if (skeleton->size == 0) {
			return RID();
		}
		if (skeleton->use_2d) {
			return RID();
		}
		if (!skeleton->uniform_set_3d.is_valid()) {
			Vector<RD::Uniform> uniforms;
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = 0;
			u.append_id(skeleton->buffer);
			uniforms.push_back(u);
			skeleton->uniform_set_3d = RD::get_singleton()->uniform_set_create(uniforms, p_shader, p_set);
		}

		return skeleton->uniform_set_3d;
	}
};

} // namespace RendererRD
