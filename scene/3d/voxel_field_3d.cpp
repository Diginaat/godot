/**************************************************************************/
/*  voxel_field_3d.cpp                                                    */
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

#include "voxel_field_3d.h"

#include "core/math/math_funcs_binary.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

// One workgroup per brick. Every lane picks the same level with the
// far-field rule: the coarsest level whose
// voxels project to at most `voxel pixels` at the brick's nearest point. The
// lanes then walk that level's voxels, frustum-test each splat and append the
// visible ones to the MultiMesh instance buffer (3x4 transform rows, colour,
// custom data with the normal) with one global atomic per 64 voxels. A second
// single-invocation mode clamps the count into the indirect draw commands.
static const char *voxel_select_shader = R"(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer Bricks {
	uint bricks[];
};
layout(set = 0, binding = 1, std430) restrict readonly buffer Voxels {
	uvec4 voxels[];
};
layout(set = 0, binding = 2, std430) restrict writeonly buffer Instances {
	vec4 instances[];
};
layout(set = 0, binding = 3, std430) restrict buffer Counter {
	uint counter;
};
layout(set = 0, binding = 4, std430) restrict buffer Commands {
	uint commands[];
};
layout(set = 0, binding = 5, std140) uniform Params {
	vec4 planes[6];
	vec4 camera;
	vec4 field;
	vec4 selection;
	uvec4 info;
	uvec4 misc;
}
params;

layout(push_constant, std430) uniform Push {
	uint mode;
	uint pad0;
	uint pad1;
	uint pad2;
}
push;

shared uint group_count;
shared uint group_base;

vec3 decode_normal(uint p_packed) {
	vec2 f = vec2(float(int(p_packed << 16u) >> 16), float(int(p_packed) >> 16)) / 32767.0;
	vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
	if (n.z < 0.0) {
		vec2 s = vec2(f.x >= 0.0 ? 1.0 : -1.0, f.y >= 0.0 ? 1.0 : -1.0);
		n.xy = (1.0 - abs(f.yx)) * s;
	}
	return normalize(n);
}

vec3 srgb_to_linear(vec3 p_color) {
	return mix(pow((p_color + 0.055) / 1.055, vec3(2.4)), p_color / 12.92, lessThan(p_color, vec3(0.04045)));
}

bool outside(vec3 p_center, float p_radius) {
	for (int i = 0; i < 6; i++) {
		if (dot(params.planes[i].xyz, p_center) - params.planes[i].w > p_radius) {
			return true;
		}
	}
	return false;
}

void main() {
	if (push.mode == 1u) {
		uint count = min(counter, params.misc.x);
		for (uint surface = 0u; surface < params.misc.y; surface++) {
			commands[surface * 5u + 1u] = count;
		}
		return;
	}
	// Uniform per workgroup: early returns before the barriers are safe.
	uint brick = gl_WorkGroupID.x + gl_WorkGroupID.y * params.misc.z;
	if (brick >= params.info.x) {
		return;
	}
	uint levels = params.info.y;
	uint stride = 1u + 2u * levels;
	uint packed = bricks[brick * stride];
	float brick_size = params.field.w / float(params.info.z);
	vec3 cell = vec3(float(packed & 1023u), float((packed >> 10u) & 1023u), float((packed >> 20u) & 1023u));
	vec3 center = params.field.xyz + (cell + 0.5) * brick_size;
	// Half the brick diagonal, plus half a coarsest voxel for the splats.
	float radius = brick_size * 1.3660254;
	bool frustum = params.selection.z != 0.0;
	if (frustum && outside(center, radius)) {
		return;
	}
	float pixels_per_unit = params.camera.w;
	if (params.selection.y == 0.0) {
		float nearest = max(length(center - params.camera.xyz) - radius, 1e-4);
		pixels_per_unit = params.camera.w / nearest;
	}
	uint level = 0u;
	for (uint l = 0u; l < levels; l++) {
		float voxel_size = params.field.w / float(params.info.w >> l);
		if (voxel_size * pixels_per_unit > params.selection.x) {
			break;
		}
		level = l;
	}
	uint first = bricks[brick * stride + 1u + 2u * level];
	uint count = bricks[brick * stride + 2u + 2u * level];
	vec3 brick_min = params.field.xyz + cell * brick_size;
	float voxel_size = params.field.w / float(params.info.w >> level);
	uint max_splats = params.misc.x;
	uint lane = gl_LocalInvocationID.x;
	for (uint base = 0u; base < count; base += 64u) {
		uint i = base + lane;
		bool visible = i < count;
		uvec4 voxel = uvec4(0u);
		vec3 position = vec3(0.0);
		float half_size = 0.0;
		if (visible) {
			voxel = voxels[first + i];
			// Cell inside the brick (6 bits per axis), then the centroid inside
			// the cell in 1/16 steps (4 bits per axis).
			vec3 c = vec3(float(voxel.x & 63u), float((voxel.x >> 6u) & 63u), float((voxel.x >> 12u) & 63u));
			vec3 o = (vec3(float((voxel.x >> 18u) & 15u), float((voxel.x >> 22u) & 15u), float((voxel.x >> 26u) & 15u)) + 0.5) / 16.0;
			position = brick_min + (c + o) * voxel_size;
			half_size = uintBitsToFloat(voxel.w);
			// The camera-facing quad reaches sqrt(2) half sizes from its centre.
			visible = !frustum || !outside(position, half_size * 1.4142136);
		}
		if (lane == 0u) {
			group_count = 0u;
		}
		barrier();
		uint slot = 0u;
		if (visible) {
			slot = atomicAdd(group_count, 1u);
		}
		barrier();
		if (lane == 0u) {
			group_base = atomicAdd(counter, group_count);
		}
		barrier();
		uint index = group_base + slot;
		if (visible && index < max_splats) {
			float h = max(half_size, 1e-6);
			vec3 color = vec3(float(voxel.y & 255u), float((voxel.y >> 8u) & 255u), float((voxel.y >> 16u) & 255u)) / 255.0;
			uint o = index * 5u;
			instances[o + 0u] = vec4(h, 0.0, 0.0, position.x);
			instances[o + 1u] = vec4(0.0, h, 0.0, position.y);
			instances[o + 2u] = vec4(0.0, 0.0, h, position.z);
			instances[o + 3u] = vec4(srgb_to_linear(color), 1.0);
			instances[o + 4u] = vec4(decode_normal(voxel.z), 0.0);
		}
		barrier();
	}
}
)";

// The instance basis is a uniform scale by the splat's half size, so the quad
// is rebuilt from the corner in UV facing whichever camera renders it, shadow
// cameras included. The normal comes from the instance's custom data. Splats
// are discs with the area of the square of that half size (radius
// 2 / sqrt(pi) half sizes): round blobs read as foliage, squares as blocks.
static const char *voxel_splat_material_shader = R"(shader_type spatial;
render_mode cull_disabled;

void vertex() {
	float half_size = length(MODEL_MATRIX[0].xyz) * 1.1283792;
	vec3 offset = (INV_VIEW_MATRIX[0].xyz * UV.x + INV_VIEW_MATRIX[1].xyz * UV.y) * half_size;
	VERTEX = inverse(mat3(MODEL_MATRIX)) * offset;
	NORMAL = INSTANCE_CUSTOM.xyz;
}

void fragment() {
	if (dot(UV, UV) > 1.0) {
		discard;
	}
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 1.0;
	SPECULAR = 0.2;
}
)";

void VoxelField3D::_make_splat_mesh() {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	const Vector2 corners[4] = { Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, 1) };
	for (int i = 0; i < 4; i++) {
		vertices.push_back(Vector3());
		normals.push_back(Vector3(0, 1, 0));
		uvs.push_back(corners[i]);
	}
	// Clockwise as seen from the camera (Godot's front face): a back face would
	// flip NORMAL under cull_disabled and lose direct light.
	const PackedInt32Array indices({ 0, 2, 1, 0, 3, 2 });
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(voxel_splat_material_shader);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	mesh->surface_set_material(0, material);
	splat_mesh = mesh;
	splat_material = material;
}

int VoxelField3D::_get_wanted_capacity() const {
	if (max_splats > 0) {
		return max_splats;
	}
	// Grows with the count the GPU asked for (read back a few frames late), up
	// to the most one selection can produce. Coarse far fields never pay for
	// their finest level; a field that grows draws its first splats only for
	// those few frames.
	const int most = MAX(1, data.is_valid() ? data->get_max_splat_count() : 0);
	const int requested = int(MIN(gpu->splat_count.get(), uint32_t(INT32_MAX / 2)));
	if (capacity > 0 && capacity >= requested && capacity <= most) {
		return capacity;
	}
	const int start = MIN(most, 1024);
	return MIN(most, MAX(start, int(Math::next_power_of_2(uint32_t(requested + requested / 4)))));
}

void VoxelField3D::_data_changed() {
	data_dirty = true;
	RenderingServer::get_singleton()->multimesh_set_custom_aabb(multimesh, get_aabb());
	update_gizmos();
	update_configuration_warnings();
}

void VoxelField3D::_queue_selection() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!is_visible_in_tree() || !rs->get_rendering_device()) {
		return;
	}
	Viewport *viewport = get_viewport();
	Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
	if (!camera) {
		return;
	}
	const Vector<Plane> planes = camera->get_frustum();
	if (planes.size() != 6) {
		return;
	}
	const int wanted = _get_wanted_capacity();
	if (wanted != capacity) {
		capacity = wanted;
		// Indirect: the selection pass writes the instance count on the GPU.
		rs->multimesh_allocate_data(multimesh, capacity, RSE::MULTIMESH_TRANSFORM_3D, true, true, true);
		rs->multimesh_set_mesh(multimesh, splat_mesh->get_rid());
	}
	// Outside the visibility range nothing draws: select nothing, and skip the
	// work entirely once the count is zero. Same rule as the renderer's
	// (distance to the AABB centre), with the fade margins counted as inside.
	const float begin = get_visibility_range_begin();
	const float end = get_visibility_range_end();
	bool in_range = true;
	if (begin > 0.0f || end > 0.0f) {
		const float distance = camera->get_global_position().distance_to(get_global_transform().xform(get_aabb().get_center()));
		in_range = !(begin > 0.0f && distance < begin - get_visibility_range_begin_margin()) && !(end > 0.0f && distance > end + get_visibility_range_end_margin());
	}
	if (!in_range && selection_empty && !data_dirty) {
		return;
	}
	selection_empty = !in_range;
	const bool usable = data.is_valid() && data->is_valid();
	// Selection runs in the node's local space, the field's own space.
	const Transform3D to_local = get_global_transform().affine_inverse();
	const Vector3 eye = to_local.xform(camera->get_global_position());
	Params params = {};
	for (int i = 0; i < 6; i++) {
		const Plane plane = to_local.xform(planes[i]);
		params.planes[i][0] = plane.normal.x;
		params.planes[i][1] = plane.normal.y;
		params.planes[i][2] = plane.normal.z;
		params.planes[i][3] = plane.d;
	}
	// Pixels per local unit at local distance 1 (perspective), or per local
	// unit (orthogonal, where the node's scale enters directly).
	const Projection projection = camera->get_camera_projection();
	const bool orthogonal = camera->get_projection() == Camera3D::PROJECTION_ORTHOGONAL;
	float pixel_scale = 0.5f * viewport->get_visible_rect().size.y * projection.columns[1][1];
	if (orthogonal) {
		const Vector3 scale = get_global_transform().basis.get_scale().abs();
		pixel_scale *= MIN(scale.x, MIN(scale.y, scale.z));
	}
	params.camera[0] = eye.x;
	params.camera[1] = eye.y;
	params.camera[2] = eye.z;
	params.camera[3] = pixel_scale;
	params.selection[0] = voxel_pixels;
	params.selection[1] = orthogonal ? 1.0f : 0.0f;
	params.selection[2] = frustum_culling ? 1.0f : 0.0f;
	params.selection[3] = in_range ? 1.0f : 0.0f;
	if (usable) {
		const Vector3 origin = data->get_origin();
		params.field[0] = origin.x;
		params.field[1] = origin.y;
		params.field[2] = origin.z;
		params.field[3] = data->get_size();
		params.info[0] = uint32_t(data->get_brick_count());
		params.info[1] = uint32_t(data->get_level_count());
		params.info[2] = uint32_t(data->get_brick_resolution());
		params.info[3] = uint32_t(data->get_resolution());
	}
	params.misc[0] = uint32_t(capacity);
	params.misc[1] = uint32_t(splat_mesh->get_surface_count());
	params.misc[2] = MIN(params.info[0], 65535u);
	PackedByteArray bytes;
	bytes.resize(sizeof(Params));
	memcpy(bytes.ptrw(), &params, sizeof(Params));

	const bool upload = data_dirty;
	data_dirty = false;
	rs->call_on_render_thread(callable_mp_static(&VoxelField3D::_render_select).bind(gpu, multimesh, bytes, upload && usable ? data->get_bricks() : PackedInt32Array(), upload && usable ? data->get_voxels() : PackedByteArray(), upload));
}

void VoxelField3D::_render_select(Ref<VoxelField3DGPU> p_gpu, RID p_multimesh, const PackedByteArray &p_params, const PackedInt32Array &p_bricks, const PackedByteArray &p_voxels, bool p_upload) {
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd || p_gpu.is_null() || p_gpu->failed || p_params.size() != int(sizeof(Params))) {
		return;
	}
	VoxelField3DGPU &gpu = *p_gpu.ptr();
	if (gpu.pipeline.is_null()) {
		String error;
		const Vector<uint8_t> spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, voxel_select_shader, RD::SHADER_LANGUAGE_GLSL, &error);
		if (spirv.is_empty()) {
			ERR_PRINT("VoxelField3D: the selection shader failed to compile: " + error);
			gpu.failed = true;
			return;
		}
		Vector<RD::ShaderStageSPIRVData> stages;
		RD::ShaderStageSPIRVData stage;
		stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
		stage.spirv = spirv;
		stages.push_back(stage);
		gpu.shader = rd->shader_create_from_spirv(stages, "VoxelField3D selection");
		if (gpu.shader.is_null()) {
			gpu.failed = true;
			return;
		}
		gpu.pipeline = rd->compute_pipeline_create(gpu.shader);
		gpu.params = rd->uniform_buffer_create(sizeof(Params));
		gpu.counter = rd->storage_buffer_create(sizeof(uint32_t));
	}
	Params params;
	memcpy(&params, p_params.ptr(), sizeof(Params));
	if (p_upload || gpu.bricks.is_null()) {
		// An absent field binds small zero buffers and selects no brick.
		if (gpu.uniform_set.is_valid() && rd->uniform_set_is_valid(gpu.uniform_set)) {
			rd->free_rid(gpu.uniform_set);
		}
		gpu.uniform_set = RID();
		for (int i = 0; i < 2; i++) {
			RID &buffer = i == 0 ? gpu.bricks : gpu.voxels;
			if (buffer.is_valid()) {
				rd->free_rid(buffer);
			}
			Vector<uint8_t> bytes = i == 0 ? p_bricks.to_byte_array() : Vector<uint8_t>(p_voxels);
			if (bytes.is_empty()) {
				bytes.resize_initialized(16);
			}
			buffer = rd->storage_buffer_create(bytes.size(), bytes);
		}
		gpu.brick_count = p_bricks.is_empty() ? 0 : params.info[0];
		gpu.gpu_bytes.set(uint64_t(p_bricks.size()) * sizeof(int32_t) + uint64_t(p_voxels.size()) + sizeof(Params) + sizeof(uint32_t));
	}
	// Parameters from a frame queued before the upload may name more bricks.
	params.info[0] = MIN(params.info[0], gpu.brick_count);
	if (params.selection[3] == 0.0f) {
		params.info[0] = 0; // Outside the visibility range: select nothing.
	}
	const RID instances = RenderingServer::get_singleton()->multimesh_get_buffer_rd_rid(p_multimesh);
	const RID commands = RenderingServer::get_singleton()->multimesh_get_command_buffer_rd_rid(p_multimesh);
	if (instances.is_null() || commands.is_null()) {
		return;
	}
	if (gpu.uniform_set.is_null() || !rd->uniform_set_is_valid(gpu.uniform_set) || instances != gpu.instance_buffer || commands != gpu.command_buffer) {
		if (gpu.uniform_set.is_valid() && rd->uniform_set_is_valid(gpu.uniform_set)) {
			rd->free_rid(gpu.uniform_set);
		}
		Vector<RD::Uniform> uniforms;
		const RID buffers[5] = { gpu.bricks, gpu.voxels, instances, gpu.counter, commands };
		for (int i = 0; i < 5; i++) {
			RD::Uniform uniform;
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.binding = i;
			uniform.append_id(buffers[i]);
			uniforms.push_back(uniform);
		}
		RD::Uniform uniform;
		uniform.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		uniform.binding = 5;
		uniform.append_id(gpu.params);
		uniforms.push_back(uniform);
		gpu.uniform_set = rd->uniform_set_create(uniforms, gpu.shader, 0);
		gpu.instance_buffer = instances;
		gpu.command_buffer = commands;
	}
	rd->buffer_update(gpu.params, 0, sizeof(Params), &params);
	rd->buffer_clear(gpu.counter, 0, sizeof(uint32_t));
	const uint32_t bricks = params.info[0];
	const uint32_t width = MAX(1u, params.misc[2]);
	uint32_t push[4] = {};
	// No timestamp here: one per field orders the render graph around it,
	// which serializes every field's clear, selection and copy (149 fields in
	// the valley cost 3 ms of GPU time that way, instead of running together).
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, gpu.pipeline);
	rd->compute_list_bind_uniform_set(list, gpu.uniform_set, 0);
	if (bricks > 0) {
		rd->compute_list_set_push_constant(list, push, sizeof(push));
		rd->compute_list_dispatch(list, MIN(bricks, width), (bricks + width - 1) / width, 1);
		rd->compute_list_add_barrier(list);
	}
	push[0] = 1;
	rd->compute_list_set_push_constant(list, push, sizeof(push));
	rd->compute_list_dispatch(list, 1, 1, 1);
	rd->compute_list_end();
	// The requested count (before clamping) for statistics, one read in flight.
	if (!gpu.readback_pending) {
		gpu.readback_pending = true;
		if (rd->buffer_get_data_async(gpu.counter, callable_mp_static(&VoxelField3D::_render_count_ready).bind(p_gpu), 0, sizeof(uint32_t)) != OK) {
			gpu.readback_pending = false;
		}
	}
}

void VoxelField3D::_render_count_ready(const Vector<uint8_t> &p_data, Ref<VoxelField3DGPU> p_gpu) {
	if (p_gpu.is_null()) {
		return;
	}
	p_gpu->readback_pending = false;
	if (p_data.size() >= int(sizeof(uint32_t))) {
		uint32_t count = 0;
		memcpy(&count, p_data.ptr(), sizeof(uint32_t));
		p_gpu->splat_count.set(count);
	}
}

void VoxelField3D::_render_free(Ref<VoxelField3DGPU> p_gpu) {
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd || p_gpu.is_null()) {
		return;
	}
	VoxelField3DGPU &gpu = *p_gpu.ptr();
	if (gpu.uniform_set.is_valid() && rd->uniform_set_is_valid(gpu.uniform_set)) {
		rd->free_rid(gpu.uniform_set);
	}
	const RID owned[6] = { gpu.bricks, gpu.voxels, gpu.params, gpu.counter, gpu.pipeline, gpu.shader };
	for (const RID &rid : owned) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
	gpu.uniform_set = RID();
	gpu.bricks = RID();
	gpu.voxels = RID();
	gpu.params = RID();
	gpu.counter = RID();
	gpu.pipeline = RID();
	gpu.shader = RID();
	gpu.failed = true;
}

void VoxelField3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process_internal(false);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_queue_selection();
		} break;
	}
}

void VoxelField3D::set_data(const Ref<VoxelFieldData> &p_data) {
	if (data == p_data) {
		return;
	}
	if (data.is_valid()) {
		data->disconnect_changed(callable_mp(this, &VoxelField3D::_data_changed));
	}
	data = p_data;
	if (data.is_valid()) {
		data->connect_changed(callable_mp(this, &VoxelField3D::_data_changed));
	}
	_data_changed();
}

Ref<VoxelFieldData> VoxelField3D::get_data() const {
	return data;
}

void VoxelField3D::set_voxel_pixels(float p_pixels) {
	ERR_FAIL_COND(!Math::is_finite(p_pixels) || p_pixels <= 0.0f);
	voxel_pixels = p_pixels;
}

float VoxelField3D::get_voxel_pixels() const {
	return voxel_pixels;
}

void VoxelField3D::set_max_splats(int p_count) {
	ERR_FAIL_COND(p_count < 0 || p_count > 16777216);
	max_splats = p_count;
}

int VoxelField3D::get_max_splats() const {
	return max_splats;
}

void VoxelField3D::set_frustum_culling(bool p_enabled) {
	frustum_culling = p_enabled;
}

bool VoxelField3D::is_frustum_culling_enabled() const {
	return frustum_culling;
}

int VoxelField3D::get_splat_count() const {
	return int(MIN(gpu->splat_count.get(), uint32_t(capacity)));
}

int64_t VoxelField3D::get_gpu_memory_usage() const {
	// Instance data (3x4 transform, colour and normal per splat) plus the field.
	return int64_t(capacity) * 20 * sizeof(float) + int64_t(gpu->gpu_bytes.get());
}

Ref<Material> VoxelField3D::get_splat_material() const {
	return splat_material;
}

AABB VoxelField3D::get_aabb() const {
	return data.is_valid() ? data->get_aabb() : AABB();
}

PackedStringArray VoxelField3D::get_configuration_warnings() const {
	PackedStringArray warnings = GeometryInstance3D::get_configuration_warnings();
	if (!RenderingServer::get_singleton()->get_rendering_device()) {
		warnings.push_back(RTR("VoxelField3D selects voxels with compute shaders and draws nothing with the Compatibility renderer."));
	}
	if (data.is_valid() && !data->is_valid()) {
		warnings.push_back(RTR("The voxel field data is inconsistent and is not drawn."));
	}
	return warnings;
}

void VoxelField3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &VoxelField3D::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &VoxelField3D::get_data);
	ClassDB::bind_method(D_METHOD("set_voxel_pixels", "pixels"), &VoxelField3D::set_voxel_pixels);
	ClassDB::bind_method(D_METHOD("get_voxel_pixels"), &VoxelField3D::get_voxel_pixels);
	ClassDB::bind_method(D_METHOD("set_max_splats", "count"), &VoxelField3D::set_max_splats);
	ClassDB::bind_method(D_METHOD("get_max_splats"), &VoxelField3D::get_max_splats);
	ClassDB::bind_method(D_METHOD("set_frustum_culling", "enabled"), &VoxelField3D::set_frustum_culling);
	ClassDB::bind_method(D_METHOD("is_frustum_culling_enabled"), &VoxelField3D::is_frustum_culling_enabled);
	ClassDB::bind_method(D_METHOD("get_splat_count"), &VoxelField3D::get_splat_count);
	ClassDB::bind_method(D_METHOD("get_gpu_memory_usage"), &VoxelField3D::get_gpu_memory_usage);
	ClassDB::bind_method(D_METHOD("get_splat_material"), &VoxelField3D::get_splat_material);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "data", PROPERTY_HINT_RESOURCE_TYPE, "VoxelFieldData"), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_pixels", PROPERTY_HINT_RANGE, "0.1,64,0.1,or_greater,suffix:px"), "set_voxel_pixels", "get_voxel_pixels");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_splats", PROPERTY_HINT_RANGE, "0,16777216,1"), "set_max_splats", "get_max_splats");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "frustum_culling"), "set_frustum_culling", "is_frustum_culling_enabled");
}

VoxelField3D::VoxelField3D() {
	gpu.instantiate();
	_make_splat_mesh();
	multimesh = RenderingServer::get_singleton()->multimesh_create();
	set_base(multimesh);
	// Splats are selected for the camera's frustum: shadows of splats outside
	// it would be missing, so the field does not cast by default.
	set_cast_shadows_setting(SHADOW_CASTING_SETTING_OFF);
}

VoxelField3D::~VoxelField3D() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->call_on_render_thread(callable_mp_static(&VoxelField3D::_render_free).bind(gpu));
	set_base(RID());
	rs->free_rid(multimesh);
}
