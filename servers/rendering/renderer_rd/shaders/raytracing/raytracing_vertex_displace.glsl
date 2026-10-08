#[compute]

#version 460

#VERSION_DEFINES

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_ARB_gpu_shader_int64 : require

// Runs a ShaderMaterial's vertex() over a mesh surface and writes the result as
// an uncompressed vertex buffer (positions block, then the normal/tangent
// block), which the path tracer then uses as deformed BLAS input. Without this,
// vertex displacement only affects the rasterizer.
//
// The base variant (no RT_VERTEX_DISPLACE_CUSTOM) is an empty kernel; it only
// exists so ShaderRD expands the includes. SceneShaderRaytracing specializes
// the expanded source per material at runtime, like the custom hit groups.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "raytracing_inc.glsl"
// get_triangle_indices() in the hit include reads gl_PrimitiveID, which compute
// shaders don't have. It is unused here.
#define gl_PrimitiveID 0
#include "raytracing_hit_inc.glsl"

// Output: positions as float3 for all vertices, then packed normal (and tangent).
layout(set = 0, binding = 0, std430) restrict writeonly buffer DisplacedVertices {
	uint dst_words[];
};

layout(set = 0, binding = 1, std430) restrict readonly buffer DisplaceParams {
	GeometryData src; // The static source surface.
	mat4 model_matrix; // Object (decompressed) to world.
	mat4 view_matrix;
	mat4 inv_view_matrix;
	mat4 projection_matrix;
	mat4 inv_projection_matrix;
	uint64_t material_address; // CustomMaterialUniforms (BDA).
	uint vertex_count;
	uint has_normal;
	uint has_tangent;
	uint dst_tbn_base_words; // Start of the normal/tangent block in dst_words.
	float time;
	float prev_time;
	vec2 viewport_size;
	uint _pad[2];
}
params;

#ifdef RT_VERTEX_DISPLACE_CUSTOM

#include "raytracing_custom_globals_inc.glsl"

// Vertex built-ins the closest hit doesn't declare.
float global_prev_time = 0.0;
vec4 position = vec4(0.0);
float rt_point_size = 1.0;
int rt_instance_id = 0;
int rt_vertex_id = 0;
mat3 model_normal_matrix = mat3(1.0);
vec4 instance_custom = vec4(0.0);
vec4 custom0_attrib = vec4(0.0);
vec4 custom1_attrib = vec4(0.0);
vec4 custom2_attrib = vec4(0.0);
vec4 custom3_attrib = vec4(0.0);

#endif

// Matches fetch_tbn_uncompressed_vertex(): 16-bit octahedral normal.
uint encode_normal(vec3 n) {
	return packUnorm2x16(vec3_to_oct(normalize(n)) * 0.5 + 0.5);
}

// Matches fetch_tbn_uncompressed_vertex(): oct.y carries the bitangent sign.
uint encode_tangent(vec3 t, float bitangent_sign) {
	vec2 oct = vec3_to_oct(normalize(t));
	float y = (oct.y * 0.5 + 0.5) * (bitangent_sign < 0.0 ? -1.0 : 1.0);
	return packUnorm2x16(vec2(oct.x, y) * 0.5 + 0.5);
}

void main() {
	uint v = gl_GlobalInvocationID.x;
	if (v >= params.vertex_count) {
		return;
	}

	GeometryData g = params.src;
	mat4 aabb_xform;
	mat4 inv_aabb_xform;
	get_aabb_compression_xforms(g, aabb_xform, inv_aabb_xform);

	vec3 object_pos;
	if ((g.flags & FLAG_COMPRESSED) != 0u) {
		Uint32Buffer vb = Uint32Buffer(g.vertex_address);
		uint w0 = vb.v[v * 2u];
		uint w1 = vb.v[v * 2u + 1u];
		vec3 q = vec3(float(w0 & 0xFFFFu), float(w0 >> 16u), float(w1 & 0xFFFFu)) / 65535.0;
		object_pos = (aabb_xform * vec4(q, 1.0)).xyz;
	} else {
		FloatBuffer fb = FloatBuffer(g.vertex_address);
		uint base = v * (g.position_stride >> 2u);
		object_pos = vec3(fb.v[base], fb.v[base + 1u], fb.v[base + 2u]);
	}

	TBNResult tbn = fetch_tbn(g, v, v, v, vec3(1.0, 0.0, 0.0));
	vec3 out_pos = object_pos;
	vec3 out_normal = tbn.normal;
	vec3 out_tangent = tbn.tangent;
	float out_bitangent_sign = tbn.bitangent_sign;

#ifdef RT_VERTEX_DISPLACE_CUSTOM
	material = CustomMaterialUniforms(params.material_address);
	vertex = object_pos;
	normal = tbn.normal;
	tangent = tbn.tangent;
	binormal = tbn.binormal;
	uv_interp = fetch_uv(g, v, v, v, vec3(1.0, 0.0, 0.0));
	uv2_interp = uv_interp;
	color_interp = fetch_color(g, v, v, v, vec3(1.0, 0.0, 0.0));
	read_model_matrix = params.model_matrix;
	model_normal_matrix = mat3(params.model_matrix);
	read_view_matrix = params.view_matrix;
	inv_view_matrix = params.inv_view_matrix;
	projection_matrix = params.projection_matrix;
	inv_projection_matrix = params.inv_projection_matrix;
	read_viewport_size = params.viewport_size;
	global_time = params.time;
	global_prev_time = params.prev_time;
	rt_vertex_id = int(v);

	rt_run_vertex_shader();

	out_pos = vertex;
	out_normal = normal;
	out_tangent = tangent;
#endif

	dst_words[v * 3u + 0u] = floatBitsToUint(out_pos.x);
	dst_words[v * 3u + 1u] = floatBitsToUint(out_pos.y);
	dst_words[v * 3u + 2u] = floatBitsToUint(out_pos.z);

	if (params.has_normal != 0u) {
		uint stride = params.has_tangent != 0u ? 2u : 1u;
		uint base = params.dst_tbn_base_words + v * stride;
		dst_words[base] = encode_normal(out_normal);
		if (params.has_tangent != 0u) {
			dst_words[base + 1u] = encode_tangent(out_tangent, out_bitangent_sign);
		}
	}
}
