// Shared defines and common bindings for all RT shader stages.
// Include AFTER raytracing_inc.glsl and scene_data_inc.glsl.
// The includer must set exactly one of RT_STAGE_{RAYGEN,MISS,CLOSEST_HIT,ANY_HIT,INTERSECTION}.

// Specialization constant (bits 0-20: flags, 21-28: samples, 29-31: bounces).
layout(constant_id = 0) const uint RT_FLAGS = 0u;

#define RT_FLAG_DLSS_RR_ENABLED (1u << 1)
#define RT_FLAG_FOG_ENABLED (1u << 2)
#define RT_FLAG_NATIVE_RR_ENABLED (1u << 5)

#define RT_SAMPLE_COUNT_SHIFT 21u
#define RT_SAMPLE_COUNT_MASK 0xFFu
#define RT_GET_SAMPLE_COUNT() max(1u, (RT_FLAGS >> RT_SAMPLE_COUNT_SHIFT) & RT_SAMPLE_COUNT_MASK)

#define RT_MAX_BOUNCES_SHIFT 29u
#define RT_MAX_BOUNCES_MASK 0x7u
#define RT_GET_MAX_BOUNCES() (((RT_FLAGS >> RT_MAX_BOUNCES_SHIFT) & RT_MAX_BOUNCES_MASK) + 1u)

#define RT_PARAM_VOLUMETRIC_FOG_INV_LENGTH 4u
#define RT_PARAM_VOLUMETRIC_FOG_DETAIL_SPREAD 5u
#define RT_PARAM_HAS_VOLUMETRIC_FOG 6u
#define RT_PARAM_VOLUMETRIC_FOG_SKY_AFFECT 7u
#define RT_PARAM_FOG_USE_LEGACY_BLENDING 8u

// Cull back faces by default; double-sided instances override via CULL_DISABLE flag.
#define RT_RAY_FLAGS gl_RayFlagsCullBackFacingTrianglesEXT

layout(set = 0, binding = 2, std140) uniform SceneDataBlock {
	SceneData data;
	SceneData prev_data;
}
scene_data_block;

layout(set = 0, binding = 14, std430) readonly buffer GlobalShaderUniformData {
	vec4 data[256];
}
global_shader_uniforms;

#ifndef RT_STAGE_ANY_HIT

layout(set = 0, binding = 6, std140) uniform RaytracingParams {
	vec4 rt_params[4];
	mat4 prev_vp_unjittered;
	mat4 curr_vp_unjittered;
};

float get_rt_param(uint idx) {
	return rt_params[idx >> 2u][idx & 3u];
}

/// Project a world-space point to UV through an unjittered VP.
vec2 project_uv(vec3 world_pos, mat4 vp) {
	vec4 clip = vp * vec4(world_pos, 1.0);
	return clip.xy / clip.w * 0.5 + 0.5;
}

#ifdef DLSS_RR_ENABLED
layout(set = 0, binding = 9, rgba16f) uniform image2D dlss_rr_diffuse_albedo;
layout(set = 0, binding = 10, rgba16f) uniform image2D dlss_rr_specular_albedo;
layout(set = 0, binding = 11, rgba16f) uniform image2D dlss_rr_normal_roughness;
layout(set = 0, binding = 12, r16f) uniform image2D dlss_rr_specular_hit_dist;
#endif

#ifdef NATIVE_RR_ENABLED
// Native ray reconstruction inputs (written by raygen and the primary hit).
// rr_diffuse: diffuse radiance (rgb), not demodulated.
// rr_specular: specular radiance (rgb, includes refraction), a = hit distance
// of the specular ray after the primary hit (RR_MISS_DISTANCE on a miss, -1 when
// no sample took the specular lobe).
// rr_guide: x = diffuse albedo (unorm8 rgb), y = specular albedo (unorm8 rgb,
// a = 1 for transmissive surfaces), z = octahedral normal (unorm16 x2,
// vec3_to_oct), w = half2(roughness, mirror hit distance). The mirror hit
// distance is traced along the perfect reflection on smooth surfaces
// (roughness < MAX_DENOISER_SPECULAR_HIT_THRESHOLD); RR_MISS_DISTANCE on a
// miss, -1 where it isn't traced.
layout(set = 0, binding = 40, rgba16f) uniform image2D rr_diffuse;
layout(set = 0, binding = 41, rgba16f) uniform image2D rr_specular;
layout(set = 0, binding = 42, rgba32ui) uniform uimage2D rr_guide;

#define RR_MISS_DISTANCE 10000.0
#define RR_GUIDE_FLAG_TRANSMISSIVE 1u

void rr_write_guide(vec3 p_diffuse_albedo, vec3 p_specular_albedo, vec3 p_normal, float p_roughness, float p_mirror_hit_distance, uint p_flags) {
	uvec4 g;
	g.x = packUnorm4x8(vec4(p_diffuse_albedo, 0.0));
	g.y = packUnorm4x8(vec4(p_specular_albedo, (p_flags & RR_GUIDE_FLAG_TRANSMISSIVE) != 0u ? 1.0 : 0.0));
	g.z = packUnorm2x16(vec3_to_oct(normalize(p_normal)));
	g.w = packHalf2x16(vec2(clamp(p_roughness, 0.0, 1.0), min(p_mirror_hit_distance, RR_MISS_DISTANCE)));
	imageStore(rr_guide, ivec2(gl_LaunchIDEXT.xy), g);
}
#endif

// Binding 14 is reserved for GlobalShaderUniformData (declared above).
// Samplers occupy 16-27 (see raytracing_samplers_inc.glsl); velocity sits at
// the first free slot past them so we do not collide with either.
layout(set = 0, binding = 28, rg16f) uniform image2D rt_velocity_image;
layout(set = 0, binding = 29) uniform sampler3D volumetric_fog_texture;
layout(set = 0, binding = 15, r32f) uniform image2D rt_depth_image;

vec4 sample_primary_volumetric_fog(float view_depth) {
	vec2 uv = (vec2(gl_LaunchIDEXT.xy) + vec2(0.5)) / vec2(gl_LaunchSizeEXT.xy);
	float z = view_depth * get_rt_param(RT_PARAM_VOLUMETRIC_FOG_INV_LENGTH);
	if (z < 0.0) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	if (z < 1.0) {
		z = pow(z, get_rt_param(RT_PARAM_VOLUMETRIC_FOG_DETAIL_SPREAD));
	}
	return texture(volumetric_fog_texture, vec3(uv, z));
}

#endif // !RT_STAGE_ANY_HIT

// Shared hitAttributeEXT layout for all hit-group stages.
// Vulkan requires every shader in a hit group to agree on this layout.
#if defined(RT_STAGE_CLOSEST_HIT) || defined(RT_STAGE_ANY_HIT) || defined(RT_STAGE_INTERSECTION)
struct HitAttribs {
	vec2 bary_or_uv;
#ifdef ENABLE_INTERSECTION_SHADERS
	uint packed_normal; // xyz: snorm8 normal,  w: delta.x FP16 low byte.
	uint packed_tangent; // xyz: snorm8 tangent, w: delta.x FP16 high byte.
	uint prev_pos_delta_yz; // packHalf2x16(delta.y, delta.z).
#endif
};
hitAttributeEXT HitAttribs hit_attribs;
#endif
