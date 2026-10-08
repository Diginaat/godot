// Shared defines and common bindings for all RT shader stages.
// Include AFTER raytracing_inc.glsl and scene_data_inc.glsl.
// The includer must set exactly one of RT_STAGE_{RAYGEN,MISS,CLOSEST_HIT,ANY_HIT,INTERSECTION}.

// Specialization constant (bits 0-20: flags, 21-28: samples, 29-31: bounces).
layout(constant_id = 0) const uint RT_FLAGS = 0u;

#define RT_FLAG_DLSS_RR_ENABLED (1u << 1)
#define RT_FLAG_FOG_ENABLED (1u << 2)

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

#ifdef USE_RESTIR_DI
// ReSTIR DI (see raytracing_restir_di_inc.glsl). Entries [0, RESTIR_LIGHTS_MAX)
// map last frame's analytic light indices to this frame's, the rest emissive
// meshes; 0xFFFFFFFF where the light is gone. Reservoirs ping-pong between _0
// and _1 by frame parity. shadercallcoherent: samples 1+ of a pixel read the
// reservoir its sample 0 stored earlier in the same launch.
#define RESTIR_LIGHTS_MAX 64u
layout(set = 0, binding = 34, std430) readonly buffer RestirLightRemap {
	uint restir_light_remap[];
};
layout(set = 0, binding = 35, rgba32ui) shadercallcoherent uniform uimage2D restir_sample_0;
layout(set = 0, binding = 36, rgba32ui) shadercallcoherent uniform uimage2D restir_sample_1;
layout(set = 0, binding = 37, rgba32f) shadercallcoherent uniform image2D restir_surface_0;
layout(set = 0, binding = 38, rgba32f) shadercallcoherent uniform image2D restir_surface_1;

// Marks this pixel as having no reservoir this frame (for pixels that never
// reach shade_and_bounce(): sky, glass). Primary ray, sample 0 only.
void restir_clear_pixel(uint p_packed_bounces_flags) {
	if (get_total_bounces(p_packed_bounces_flags) != 0u || !is_sample_zero(p_packed_bounces_flags)) {
		return;
	}
	ivec2 p = ivec2(gl_LaunchIDEXT.xy);
	if ((uint(get_rt_param(RT_PARAM_FRAME_INDEX)) & 1u) != 0u) {
		imageStore(restir_sample_1, p, uvec4(0xFFFFFFFFu, 0u, 0u, 0u));
	} else {
		imageStore(restir_sample_0, p, uvec4(0xFFFFFFFFu, 0u, 0u, 0u));
	}
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
