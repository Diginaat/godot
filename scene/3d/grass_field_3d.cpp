/**************************************************************************/
/*  grass_field_3d.cpp                                                    */
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

#include "grass_field_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

// One invocation per patch of a camera-centred square grid. A visible patch
// allocates its blades with one atomic add and writes them straight into the
// MultiMesh instance buffer (3x4 transform rows, then the colour); a second
// single-invocation mode clamps the count into the indirect draw commands.
// Blades are pure functions of (patch, blade, seed), so the same blades
// reappear when the camera returns and nothing is stored between frames.
// With edge dithering each blade also has a hash in [0, 1) that decides
// whether it survives a fade band: the outer band keeps it while the hash is
// at least the band's progress, the inner band while it is below. Two fields
// with the same seed, patch size and blade count place the same blades, so
// one field's outer band and another's matching inner band hand every blade
// over exactly once (level of detail rings without gaps or doubles).
static const char *grass_generate_shader = R"(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer Heights {
	float heights[];
};
layout(set = 0, binding = 1, std430) restrict readonly buffer Density {
	float density[];
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
	vec4 grid;
	vec4 blade;
	vec4 color_a;
	vec4 color_b;
	ivec4 grid_size;
	ivec4 patches;
	vec4 patch_info;
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

uint hash(uint v) {
	// PCG output permutation.
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float next_random(inout uint state) {
	state = hash(state);
	return float(state >> 8u) * (1.0 / 16777216.0);
}

// Grid values interpolated over the two triangles of each cell, split along
// the diagonal from (x + 1, z) to (x, z + 1), like a terrain mesh built with
// indices (a, a + 1, a + w) and (a + 1, a + w + 1, a + w).
float grid_value(bool p_density, vec2 p_position) {
	ivec2 size = params.grid_size.xy;
	vec2 g = clamp((p_position - params.grid.xy) / params.grid.z, vec2(0.0), vec2(size - 1) - 1e-4);
	ivec2 cell = ivec2(g);
	vec2 f = g - vec2(cell);
	int a = cell.y * size.x + cell.x;
	float v00 = p_density ? density[a] : heights[a];
	float v10 = p_density ? density[a + 1] : heights[a + 1];
	float v01 = p_density ? density[a + size.x] : heights[a + size.x];
	float v11 = p_density ? density[a + size.x + 1] : heights[a + size.x + 1];
	if (f.x + f.y <= 1.0) {
		return v00 + f.x * (v10 - v00) + f.y * (v01 - v00);
	}
	return v11 + (1.0 - f.x) * (v01 - v11) + (1.0 - f.y) * (v10 - v11);
}

float height_at(vec2 p_position) {
	return params.grid_size.z != 0 ? grid_value(false, p_position) : 0.0;
}

void main() {
	if (push.mode == 1u) {
		uint count = min(counter, params.misc.x);
		for (uint surface = 0u; surface < params.misc.z; surface++) {
			commands[surface * 5u + 1u] = count;
		}
		return;
	}
	uint side = uint(params.patches.z);
	uint index = gl_GlobalInvocationID.x;
	if (index >= side * side) {
		return;
	}
	ivec2 patch_id = params.patches.xy + ivec2(index % side, index / side);
	float patch_size = params.patch_info.x;
	vec2 corner = vec2(patch_id) * patch_size;
	vec2 center = corner + 0.5 * patch_size;
	float radius = params.camera.w;
	float distance_xz = length(center - params.camera.xz);
	bool dither = params.misc.w != 0u;
	// Dithered edges decide per blade, so only patches wholly outside are skipped.
	if (distance_xz - (dither ? 0.7072 * patch_size : 0.0) >= radius) {
		return;
	}
	float inner_radius = params.patch_info.z;
	float inner_start = params.patch_info.w;
	if (inner_radius > 0.0 && distance_xz + 0.7072 * patch_size < inner_start) {
		return;
	}
	bool gridded = params.grid_size.z != 0 || params.grid_size.w != 0;
	if (gridded) {
		vec2 g = (center - params.grid.xy) / params.grid.z;
		if (any(lessThan(g, vec2(0.0))) || any(greaterThan(g, vec2(params.grid_size.xy - 1)))) {
			return;
		}
	}
	float amount = params.grid_size.w != 0 ? clamp(grid_value(true, center), 0.0, 1.0) : 1.0;
	// Fewer blades towards the edge of the radius, continuously: the last
	// blade of a patch narrows instead of popping.
	float fade_start = params.patch_info.y;
	if (!dither && fade_start < radius) {
		amount *= clamp((radius - distance_xz) / (radius - fade_start), 0.0, 1.0);
	}
	float wanted = amount * float(params.patches.w);
	if (wanted <= 0.0) {
		return;
	}
	// Conservative bounds: the patch's lowest and highest corner, plus the
	// tallest blade leaning over the patch border.
	float h0 = height_at(corner);
	float h1 = height_at(corner + vec2(patch_size, 0.0));
	float h2 = height_at(corner + vec2(0.0, patch_size));
	float h3 = height_at(corner + vec2(patch_size));
	float low = min(min(h0, h1), min(h2, h3));
	float high = max(max(h0, h1), max(h2, h3)) + params.blade.y;
	vec3 sphere_center = vec3(center.x, 0.5 * (low + high), center.y);
	float sphere_radius = 0.7072 * patch_size + 0.5 * (high - low) + params.blade.y * sin(params.blade.w);
	for (int i = 0; i < 6; i++) {
		if (dot(params.planes[i].xyz, sphere_center) - params.planes[i].w > sphere_radius) {
			return;
		}
	}
	uint count = uint(ceil(wanted));
	float last_width = wanted - floor(wanted);
	uint patch_seed = hash(uint(patch_id.x) * 73856093u ^ hash(uint(patch_id.y) ^ params.misc.y));
	// Blades this field keeps, as a bit mask (blades_per_patch is at most 256).
	uint keep[8] = uint[8](0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
	uint kept = 0u;
	for (uint b = 0u; b < count; b++) {
		bool keep_blade = true;
		if (dither) {
			uint state = hash(patch_seed ^ (b * 2654435761u));
			vec2 position = corner + vec2(next_random(state), next_random(state)) * patch_size;
			float d = length(position - params.camera.xz);
			float h = float(hash(state ^ 0x68bc21ebu) >> 8u) * (1.0 / 16777216.0);
			if (fade_start < radius) {
				keep_blade = h >= clamp((d - fade_start) / (radius - fade_start), 0.0, 1.0);
			} else {
				keep_blade = d < radius;
			}
			if (inner_radius > 0.0) {
				float t = inner_radius > inner_start ? clamp((d - inner_start) / (inner_radius - inner_start), 0.0, 1.0) : (d >= inner_radius ? 1.0 : 0.0);
				keep_blade = keep_blade && h < t;
			}
		}
		if (keep_blade) {
			keep[b >> 5u] |= 1u << (b & 31u);
			kept++;
		}
	}
	if (kept == 0u) {
		return;
	}
	uint first = atomicAdd(counter, kept);
	uint max_blades = params.misc.x;
	if (first >= max_blades) {
		return;
	}
	uint written = min(kept, max_blades - first);
	uint slot = 0u;
	for (uint b = 0u; b < count && slot < written; b++) {
		if ((keep[b >> 5u] & (1u << (b & 31u))) == 0u) {
			continue;
		}
		uint state = hash(patch_seed ^ (b * 2654435761u));
		vec2 position = corner + vec2(next_random(state), next_random(state)) * patch_size;
		float yaw = next_random(state) * 6.2831853;
		float height = mix(params.blade.x, params.blade.y, next_random(state));
		float lean = next_random(state) * params.blade.w;
		float tint = next_random(state);
		float width = params.blade.z * ((b + 1u == count && last_width > 0.0) ? last_width : 1.0);
		float cy = cos(yaw);
		float sy = sin(yaw);
		float cl = cos(lean);
		float sl = sin(lean);
		// Basis rows of yaw * lean * scale(width, height, width).
		uint base = (first + slot) * 4u;
		slot++;
		instances[base + 0u] = vec4(cy * width, sy * sl * height, sy * cl * width, position.x);
		instances[base + 1u] = vec4(0.0, cl * height, -sl * width, height_at(position));
		instances[base + 2u] = vec4(-sy * width, cy * sl * height, cy * cl * width, position.y);
		instances[base + 3u] = mix(params.color_a, params.color_b, tint);
	}
}
)";

// Lit from above on both sides: the fragment normal ignores the facing, so
// back faces of the double-sided blades are not darkened by the flipped normal.
// With the weather module, the blades bend in the WindServer's wind.
static const char *grass_default_material_shader = R"(shader_type spatial;
render_mode cull_disabled;

#ifdef WIND
#include "res://.builtin/wind.gdshaderinc"

uniform float wind_strength : hint_range(0.0, 2.0) = 1.0;
// Wind speed in m/s that bends the blades two thirds of the way.
uniform float wind_stiffness : hint_range(0.5, 30.0) = 5.0;
#endif

varying float blade_height;

void vertex() {
	blade_height = UV.y;
#ifdef WIND
	vec3 root = MODEL_MATRIX[3].xyz;
	float height = length(MODEL_MATRIX[1].xyz);
	VERTEX += wind_to_local(MODEL_MATRIX, wind_grass_offset(root, UV.y, height, wind_stiffness, TIME) * wind_strength);
#endif
}

void fragment() {
	ALBEDO = COLOR.rgb * mix(0.55, 1.0, blade_height);
	ROUGHNESS = 0.9;
	SPECULAR = 0.15;
	NORMAL = normalize((VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
}
)";

void GrassField3D::_make_default_mesh() {
	// A unit blade: tapered strip of three segments and a tip, base at the origin.
	const float heights[4] = { 0.0f, 0.35f, 0.65f, 0.88f };
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	for (int i = 0; i < 4; i++) {
		const float half = 0.5f * Math::pow(1.0f - heights[i], 0.7f);
		vertices.push_back(Vector3(-half, heights[i], 0.0f));
		vertices.push_back(Vector3(half, heights[i], 0.0f));
		uvs.push_back(Vector2(0.0f, heights[i]));
		uvs.push_back(Vector2(1.0f, heights[i]));
	}
	vertices.push_back(Vector3(0.0f, 1.0f, 0.0f));
	uvs.push_back(Vector2(0.5f, 1.0f));
	for (int i = 0; i < vertices.size(); i++) {
		normals.push_back(Vector3(0.0f, 1.0f, 0.0f));
	}
	PackedInt32Array indices;
	for (int i = 0; i < 3; i++) {
		const int a = i * 2;
		indices.append_array(PackedInt32Array({ a, a + 2, a + 1, a + 1, a + 2, a + 3 }));
	}
	indices.append_array(PackedInt32Array({ 6, 8, 7 }));
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
	String code = grass_default_material_shader;
	if (Engine::get_singleton()->has_singleton("WindServer")) {
		code = code.replace("render_mode cull_disabled;\n", "render_mode cull_disabled;\n#define WIND\n");
	}
	shader->set_code(code);
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	mesh->surface_set_material(0, material);
	default_mesh = mesh;
	default_material = material;
}

Ref<Mesh> GrassField3D::_get_effective_mesh() {
	if (blade_mesh.is_valid() && blade_mesh->get_surface_count() > 0) {
		return blade_mesh;
	}
	if (default_mesh.is_null()) {
		_make_default_mesh();
	}
	return default_mesh;
}

void GrassField3D::_update_allocation() {
	allocation_dirty = false;
	RenderingServer *rs = RenderingServer::get_singleton();
	const Ref<Mesh> mesh = _get_effective_mesh();
	// Indirect: the generation pass writes the instance count on the GPU.
	rs->multimesh_allocate_data(multimesh, max_blades, RSE::MULTIMESH_TRANSFORM_3D, true, false, true);
	rs->multimesh_set_mesh(multimesh, mesh->get_rid());
}

void GrassField3D::_update_height_range() {
	height_min = 0.0f;
	height_max = 0.0f;
	if (grid_size.x >= 2 && grid_size.y >= 2 && height_grid.size() == grid_size.x * grid_size.y) {
		height_min = height_max = height_grid[0];
		for (const float height : height_grid) {
			height_min = MIN(height_min, height);
			height_max = MAX(height_max, height);
		}
	}
}

void GrassField3D::_queue_generation() {
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
	if (allocation_dirty) {
		_update_allocation();
	}
	// Generation runs in the node's local space, so a moved or rotated field
	// keeps its blades; distances and sizes are local units.
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
	params.camera[0] = eye.x;
	params.camera[1] = eye.y;
	params.camera[2] = eye.z;
	params.camera[3] = radius;
	params.grid[0] = grid_origin.x;
	params.grid[1] = grid_origin.y;
	params.grid[2] = grid_cell_size;
	params.blade[0] = blade_height_min;
	params.blade[1] = MAX(blade_height_min, blade_height_max);
	params.blade[2] = blade_width;
	params.blade[3] = blade_lean;
	const Color a = color_a.srgb_to_linear();
	const Color b = color_b.srgb_to_linear();
	const float colors[2][4] = { { a.r, a.g, a.b, 1.0f }, { b.r, b.g, b.b, 1.0f } };
	memcpy(params.color_a, colors[0], sizeof(params.color_a));
	memcpy(params.color_b, colors[1], sizeof(params.color_b));
	const bool grid_valid = grid_size.x >= 2 && grid_size.y >= 2;
	const bool has_height = grid_valid && height_grid.size() == grid_size.x * grid_size.y;
	const bool has_density = grid_valid && density_grid.size() == grid_size.x * grid_size.y;
	params.grid_size[0] = grid_size.x;
	params.grid_size[1] = grid_size.y;
	params.grid_size[2] = has_height;
	params.grid_size[3] = has_density;
	const int half = int(Math::ceil(radius / patch_size));
	params.patches[0] = int(Math::floor(eye.x / patch_size)) - half;
	params.patches[1] = int(Math::floor(eye.z / patch_size)) - half;
	params.patches[2] = 2 * half + 1;
	params.patches[3] = blades_per_patch;
	params.patch[0] = patch_size;
	params.patch[1] = radius * (1.0f - fade_fraction);
	params.patch[2] = inner_radius;
	params.patch[3] = MAX(inner_radius - inner_fade, 0.0f);
	params.misc[0] = uint32_t(max_blades);
	params.misc[1] = uint32_t(seed);
	params.misc[2] = uint32_t(_get_effective_mesh()->get_surface_count());
	params.misc[3] = edge_dither ? 1u : 0u;
	PackedByteArray bytes;
	bytes.resize(sizeof(Params));
	memcpy(bytes.ptrw(), &params, sizeof(Params));

	const bool upload = grids_dirty;
	grids_dirty = false;
	rs->call_on_render_thread(callable_mp_static(&GrassField3D::_render_generate).bind(gpu, multimesh, bytes, upload && has_height ? height_grid : PackedFloat32Array(), upload && has_density ? density_grid : PackedFloat32Array(), upload));

	// Bounds of everything this frame can generate.
	const float top = (has_height ? height_max : 0.0f) + params.blade[1];
	const float bottom = has_height ? height_min : 0.0f;
	aabb = AABB(Vector3(eye.x - radius - patch_size, bottom, eye.z - radius - patch_size), Vector3(2.0f * (radius + patch_size), top - bottom, 2.0f * (radius + patch_size)));
	rs->multimesh_set_custom_aabb(multimesh, aabb);
}

void GrassField3D::_render_generate(Ref<GrassField3DGPU> p_gpu, RID p_multimesh, const PackedByteArray &p_params, const PackedFloat32Array &p_heights, const PackedFloat32Array &p_density, bool p_upload) {
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd || p_gpu.is_null() || p_gpu->failed || p_params.size() != int(sizeof(Params))) {
		return;
	}
	GrassField3DGPU &gpu = *p_gpu.ptr();
	if (gpu.pipeline.is_null()) {
		String error;
		const Vector<uint8_t> spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, grass_generate_shader, RD::SHADER_LANGUAGE_GLSL, &error);
		if (spirv.is_empty()) {
			ERR_PRINT("GrassField3D: the generation shader failed to compile: " + error);
			gpu.failed = true;
			return;
		}
		Vector<RD::ShaderStageSPIRVData> stages;
		RD::ShaderStageSPIRVData stage;
		stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
		stage.spirv = spirv;
		stages.push_back(stage);
		gpu.shader = rd->shader_create_from_spirv(stages, "GrassField3D generation");
		if (gpu.shader.is_null()) {
			gpu.failed = true;
			return;
		}
		gpu.pipeline = rd->compute_pipeline_create(gpu.shader);
		gpu.params = rd->uniform_buffer_create(sizeof(Params));
		gpu.counter = rd->storage_buffer_create(sizeof(uint32_t));
	}
	if (p_upload || gpu.heights.is_null()) {
		// Absent grids bind a small zero buffer; the shader never reads it.
		for (int i = 0; i < 2; i++) {
			RID &buffer = i == 0 ? gpu.heights : gpu.density;
			const PackedFloat32Array &values = i == 0 ? p_heights : p_density;
			if (buffer.is_valid()) {
				rd->free_rid(buffer);
			}
			Vector<uint8_t> data = values.is_empty() ? Vector<uint8_t>() : values.to_byte_array();
			if (data.is_empty()) {
				data.resize_initialized(16);
			}
			buffer = rd->storage_buffer_create(data.size(), data);
		}
		gpu.gpu_bytes.set(uint64_t(p_heights.size() + p_density.size()) * sizeof(float) + sizeof(Params) + sizeof(uint32_t));
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
		const RID buffers[5] = { gpu.heights, gpu.density, instances, gpu.counter, commands };
		for (int i = 0; i < 5; i++) {
			RD::Uniform uniform;
			uniform.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			uniform.binding = i;
			uniform.append_id(buffers[i]);
			uniforms.push_back(uniform);
		}
		RD::Uniform params;
		params.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		params.binding = 5;
		params.append_id(gpu.params);
		uniforms.push_back(params);
		gpu.uniform_set = rd->uniform_set_create(uniforms, gpu.shader, 0);
		gpu.instance_buffer = instances;
		gpu.command_buffer = commands;
	}
	Params params;
	memcpy(&params, p_params.ptr(), sizeof(Params));
	rd->buffer_update(gpu.params, 0, sizeof(Params), &params);
	rd->buffer_clear(gpu.counter, 0, sizeof(uint32_t));
	const uint32_t patches = uint32_t(params.patches[2]) * uint32_t(params.patches[2]);
	uint32_t push[4] = {};
	// No timestamp here: one per field orders the render graph around it
	// and serializes the fields' work.
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, gpu.pipeline);
	rd->compute_list_bind_uniform_set(list, gpu.uniform_set, 0);
	rd->compute_list_set_push_constant(list, push, sizeof(push));
	rd->compute_list_dispatch(list, (patches + 63) / 64, 1, 1);
	rd->compute_list_add_barrier(list);
	push[0] = 1;
	rd->compute_list_set_push_constant(list, push, sizeof(push));
	rd->compute_list_dispatch(list, 1, 1, 1);
	rd->compute_list_end();
	// The requested count (before clamping) for statistics, one read in flight.
	if (!gpu.readback_pending) {
		gpu.readback_pending = true;
		if (rd->buffer_get_data_async(gpu.counter, callable_mp_static(&GrassField3D::_render_count_ready).bind(p_gpu), 0, sizeof(uint32_t)) != OK) {
			gpu.readback_pending = false;
		}
	}
}

void GrassField3D::_render_count_ready(const Vector<uint8_t> &p_data, Ref<GrassField3DGPU> p_gpu) {
	if (p_gpu.is_null()) {
		return;
	}
	p_gpu->readback_pending = false;
	if (p_data.size() >= int(sizeof(uint32_t))) {
		uint32_t count = 0;
		memcpy(&count, p_data.ptr(), sizeof(uint32_t));
		p_gpu->blade_count.set(count);
	}
}

void GrassField3D::_render_free(Ref<GrassField3DGPU> p_gpu) {
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd || p_gpu.is_null()) {
		return;
	}
	GrassField3DGPU &gpu = *p_gpu.ptr();
	if (gpu.uniform_set.is_valid() && rd->uniform_set_is_valid(gpu.uniform_set)) {
		rd->free_rid(gpu.uniform_set);
	}
	const RID owned[6] = { gpu.heights, gpu.density, gpu.params, gpu.counter, gpu.pipeline, gpu.shader };
	for (const RID &rid : owned) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
	gpu.uniform_set = RID();
	gpu.heights = RID();
	gpu.density = RID();
	gpu.params = RID();
	gpu.counter = RID();
	gpu.pipeline = RID();
	gpu.shader = RID();
	gpu.failed = true;
}

void GrassField3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process_internal(false);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_queue_generation();
		} break;
	}
}

void GrassField3D::set_blade_mesh(const Ref<Mesh> &p_mesh) {
	blade_mesh = p_mesh;
	allocation_dirty = true;
}

Ref<Mesh> GrassField3D::get_blade_mesh() const {
	return blade_mesh;
}

void GrassField3D::set_radius(float p_radius) {
	ERR_FAIL_COND(!(p_radius > 0.0f) || p_radius > 4096.0f);
	radius = p_radius;
}

float GrassField3D::get_radius() const {
	return radius;
}

void GrassField3D::set_patch_size(float p_size) {
	ERR_FAIL_COND(!(p_size >= 0.05f) || p_size > 64.0f);
	patch_size = p_size;
}

float GrassField3D::get_patch_size() const {
	return patch_size;
}

void GrassField3D::set_blades_per_patch(int p_count) {
	ERR_FAIL_COND(p_count < 1 || p_count > 256);
	blades_per_patch = p_count;
}

int GrassField3D::get_blades_per_patch() const {
	return blades_per_patch;
}

void GrassField3D::set_max_blades(int p_count) {
	// 64 bytes per blade: 4 Mi blades are 256 MiB of instance data.
	ERR_FAIL_COND(p_count < 1 || p_count > (1 << 22));
	max_blades = p_count;
	allocation_dirty = true;
}

int GrassField3D::get_max_blades() const {
	return max_blades;
}

void GrassField3D::set_blade_height_min(float p_height) {
	blade_height_min = MAX(p_height, 0.0f);
}

float GrassField3D::get_blade_height_min() const {
	return blade_height_min;
}

void GrassField3D::set_blade_height_max(float p_height) {
	blade_height_max = MAX(p_height, 0.0f);
}

float GrassField3D::get_blade_height_max() const {
	return blade_height_max;
}

void GrassField3D::set_blade_width(float p_width) {
	blade_width = MAX(p_width, 0.0f);
}

float GrassField3D::get_blade_width() const {
	return blade_width;
}

void GrassField3D::set_blade_lean(float p_lean) {
	blade_lean = CLAMP(p_lean, 0.0f, float(Math::PI) * 0.5f);
}

float GrassField3D::get_blade_lean() const {
	return blade_lean;
}

void GrassField3D::set_color_a(const Color &p_color) {
	color_a = p_color;
}

Color GrassField3D::get_color_a() const {
	return color_a;
}

void GrassField3D::set_color_b(const Color &p_color) {
	color_b = p_color;
}

Color GrassField3D::get_color_b() const {
	return color_b;
}

void GrassField3D::set_fade_fraction(float p_fraction) {
	fade_fraction = CLAMP(p_fraction, 0.0f, 1.0f);
}

float GrassField3D::get_fade_fraction() const {
	return fade_fraction;
}

void GrassField3D::set_inner_radius(float p_radius) {
	ERR_FAIL_COND(!(p_radius >= 0.0f) || p_radius > 4096.0f);
	inner_radius = p_radius;
}

float GrassField3D::get_inner_radius() const {
	return inner_radius;
}

void GrassField3D::set_inner_fade(float p_width) {
	ERR_FAIL_COND(!(p_width >= 0.0f) || p_width > 4096.0f);
	inner_fade = p_width;
}

float GrassField3D::get_inner_fade() const {
	return inner_fade;
}

void GrassField3D::set_edge_dither(bool p_enabled) {
	edge_dither = p_enabled;
}

bool GrassField3D::is_edge_dither_enabled() const {
	return edge_dither;
}

void GrassField3D::set_seed(int p_seed) {
	seed = p_seed;
}

int GrassField3D::get_seed() const {
	return seed;
}

void GrassField3D::set_height_grid(const PackedFloat32Array &p_heights) {
	height_grid = p_heights;
	grids_dirty = true;
	_update_height_range();
	update_configuration_warnings();
}

PackedFloat32Array GrassField3D::get_height_grid() const {
	return height_grid;
}

void GrassField3D::set_density_grid(const PackedFloat32Array &p_density) {
	density_grid = p_density;
	grids_dirty = true;
	update_configuration_warnings();
}

PackedFloat32Array GrassField3D::get_density_grid() const {
	return density_grid;
}

void GrassField3D::set_grid_size(const Vector2i &p_size) {
	grid_size = p_size.maxi(0);
	grids_dirty = true;
	_update_height_range();
	update_configuration_warnings();
}

Vector2i GrassField3D::get_grid_size() const {
	return grid_size;
}

void GrassField3D::set_grid_origin(const Vector2 &p_origin) {
	grid_origin = p_origin;
}

Vector2 GrassField3D::get_grid_origin() const {
	return grid_origin;
}

void GrassField3D::set_grid_cell_size(float p_size) {
	ERR_FAIL_COND(!(p_size > 0.0f));
	grid_cell_size = p_size;
}

float GrassField3D::get_grid_cell_size() const {
	return grid_cell_size;
}

int GrassField3D::get_generated_blade_count() const {
	return int(MIN(gpu->blade_count.get(), uint32_t(max_blades)));
}

int64_t GrassField3D::get_gpu_memory_usage() const {
	// Instance data (3x4 transform and colour per blade) plus grids and parameters.
	return int64_t(max_blades) * 16 * sizeof(float) + int64_t(gpu->gpu_bytes.get());
}

AABB GrassField3D::get_aabb() const {
	return aabb;
}

PackedStringArray GrassField3D::get_configuration_warnings() const {
	PackedStringArray warnings = GeometryInstance3D::get_configuration_warnings();
	if (!RenderingServer::get_singleton()->get_rendering_device()) {
		warnings.push_back(RTR("GrassField3D generates blades with compute shaders and draws nothing with the Compatibility renderer."));
	}
	const int points = grid_size.x * grid_size.y;
	if ((!height_grid.is_empty() && height_grid.size() != points) || (!density_grid.is_empty() && density_grid.size() != points)) {
		warnings.push_back(RTR("The height or density grid does not have grid_size.x * grid_size.y values and is ignored."));
	}
	return warnings;
}

void GrassField3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_blade_mesh", "mesh"), &GrassField3D::set_blade_mesh);
	ClassDB::bind_method(D_METHOD("get_blade_mesh"), &GrassField3D::get_blade_mesh);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &GrassField3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &GrassField3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_patch_size", "size"), &GrassField3D::set_patch_size);
	ClassDB::bind_method(D_METHOD("get_patch_size"), &GrassField3D::get_patch_size);
	ClassDB::bind_method(D_METHOD("set_blades_per_patch", "count"), &GrassField3D::set_blades_per_patch);
	ClassDB::bind_method(D_METHOD("get_blades_per_patch"), &GrassField3D::get_blades_per_patch);
	ClassDB::bind_method(D_METHOD("set_max_blades", "count"), &GrassField3D::set_max_blades);
	ClassDB::bind_method(D_METHOD("get_max_blades"), &GrassField3D::get_max_blades);
	ClassDB::bind_method(D_METHOD("set_blade_height_min", "height"), &GrassField3D::set_blade_height_min);
	ClassDB::bind_method(D_METHOD("get_blade_height_min"), &GrassField3D::get_blade_height_min);
	ClassDB::bind_method(D_METHOD("set_blade_height_max", "height"), &GrassField3D::set_blade_height_max);
	ClassDB::bind_method(D_METHOD("get_blade_height_max"), &GrassField3D::get_blade_height_max);
	ClassDB::bind_method(D_METHOD("set_blade_width", "width"), &GrassField3D::set_blade_width);
	ClassDB::bind_method(D_METHOD("get_blade_width"), &GrassField3D::get_blade_width);
	ClassDB::bind_method(D_METHOD("set_blade_lean", "lean"), &GrassField3D::set_blade_lean);
	ClassDB::bind_method(D_METHOD("get_blade_lean"), &GrassField3D::get_blade_lean);
	ClassDB::bind_method(D_METHOD("set_color_a", "color"), &GrassField3D::set_color_a);
	ClassDB::bind_method(D_METHOD("get_color_a"), &GrassField3D::get_color_a);
	ClassDB::bind_method(D_METHOD("set_color_b", "color"), &GrassField3D::set_color_b);
	ClassDB::bind_method(D_METHOD("get_color_b"), &GrassField3D::get_color_b);
	ClassDB::bind_method(D_METHOD("set_fade_fraction", "fraction"), &GrassField3D::set_fade_fraction);
	ClassDB::bind_method(D_METHOD("get_fade_fraction"), &GrassField3D::get_fade_fraction);
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &GrassField3D::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &GrassField3D::get_inner_radius);
	ClassDB::bind_method(D_METHOD("set_inner_fade", "width"), &GrassField3D::set_inner_fade);
	ClassDB::bind_method(D_METHOD("get_inner_fade"), &GrassField3D::get_inner_fade);
	ClassDB::bind_method(D_METHOD("set_edge_dither", "enabled"), &GrassField3D::set_edge_dither);
	ClassDB::bind_method(D_METHOD("is_edge_dither_enabled"), &GrassField3D::is_edge_dither_enabled);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &GrassField3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &GrassField3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_height_grid", "heights"), &GrassField3D::set_height_grid);
	ClassDB::bind_method(D_METHOD("get_height_grid"), &GrassField3D::get_height_grid);
	ClassDB::bind_method(D_METHOD("set_density_grid", "density"), &GrassField3D::set_density_grid);
	ClassDB::bind_method(D_METHOD("get_density_grid"), &GrassField3D::get_density_grid);
	ClassDB::bind_method(D_METHOD("set_grid_size", "size"), &GrassField3D::set_grid_size);
	ClassDB::bind_method(D_METHOD("get_grid_size"), &GrassField3D::get_grid_size);
	ClassDB::bind_method(D_METHOD("set_grid_origin", "origin"), &GrassField3D::set_grid_origin);
	ClassDB::bind_method(D_METHOD("get_grid_origin"), &GrassField3D::get_grid_origin);
	ClassDB::bind_method(D_METHOD("set_grid_cell_size", "size"), &GrassField3D::set_grid_cell_size);
	ClassDB::bind_method(D_METHOD("get_grid_cell_size"), &GrassField3D::get_grid_cell_size);
	ClassDB::bind_method(D_METHOD("get_generated_blade_count"), &GrassField3D::get_generated_blade_count);
	ClassDB::bind_method(D_METHOD("get_gpu_memory_usage"), &GrassField3D::get_gpu_memory_usage);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blade_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_blade_mesh", "get_blade_mesh");
	ADD_GROUP("Placement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.1,4096,0.1,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "patch_size", PROPERTY_HINT_RANGE, "0.05,64,0.01,suffix:m"), "set_patch_size", "get_patch_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blades_per_patch", PROPERTY_HINT_RANGE, "1,256,1"), "set_blades_per_patch", "get_blades_per_patch");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_blades", PROPERTY_HINT_RANGE, "1,4194304,1"), "set_max_blades", "get_max_blades");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_fraction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fade_fraction", "get_fade_fraction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater,suffix:m"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_fade", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_inner_fade", "get_inner_fade");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "edge_dither"), "set_edge_dither", "is_edge_dither_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_GROUP("Blades", "blade_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blade_height_min", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_blade_height_min", "get_blade_height_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blade_height_max", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_blade_height_max", "get_blade_height_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blade_width", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_blade_width", "get_blade_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blade_lean", PROPERTY_HINT_RANGE, "0,90,0.1,radians_as_degrees"), "set_blade_lean", "get_blade_lean");
	ADD_GROUP("Colors", "color_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_a", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color_a", "get_color_a");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_b", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color_b", "get_color_b");
	ADD_GROUP("Grid", "grid_");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "grid_size"), "set_grid_size", "get_grid_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "grid_origin", PROPERTY_HINT_NONE, "suffix:m"), "set_grid_origin", "get_grid_origin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grid_cell_size", PROPERTY_HINT_RANGE, "0.001,1024,0.001,or_greater,suffix:m"), "set_grid_cell_size", "get_grid_cell_size");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "height_grid"), "set_height_grid", "get_height_grid");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "density_grid"), "set_density_grid", "get_density_grid");
}

GrassField3D::GrassField3D() {
	gpu.instantiate();
	multimesh = RenderingServer::get_singleton()->multimesh_create();
	set_base(multimesh);
	// Blades are generated for the camera's frustum only; shadows of blades
	// outside it would be missing, so they do not cast by default.
	set_cast_shadows_setting(SHADOW_CASTING_SETTING_OFF);
}

GrassField3D::~GrassField3D() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->call_on_render_thread(callable_mp_static(&GrassField3D::_render_free).bind(gpu));
	set_base(RID());
	rs->free_rid(multimesh);
}
