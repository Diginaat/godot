// DDGI resources in the ray tracing scene set (set 0, bindings 34-40).
// Bound in every RT dispatch (with small defaults when DDGI is off), so the
// path tracer pipeline layout stays the same with and without DDGI.
// Include after raytracing_common_inc.glsl, and after
// raytracing_samplers_inc.glsl when DDGI_SAMPLING is defined.

#include "ddgi_inc.glsl"

layout(set = 0, binding = 34, std140) uniform DDGIUniforms {
	DDGIDataBlock ddgi;
};

layout(set = 0, binding = 35) uniform texture2D ddgi_irradiance_atlas;
layout(set = 0, binding = 36) uniform texture2D ddgi_distance_atlas;

layout(set = 0, binding = 37, std430) readonly buffer DDGIProbes {
	DDGIProbe ddgi_probes[];
};

#ifdef RT_STAGE_RAYGEN
layout(set = 0, binding = 38, rgba16f) uniform restrict writeonly image2D ddgi_ray_data;

layout(set = 0, binding = 39, std430) readonly buffer DDGIUpdateList {
	uint ddgi_update_count;
	uint ddgi_update_pad[3];
	uint ddgi_update_probes[];
};

// Per probe ray (slot * rays per probe + ray), for the bounce light pass
// (MODE_BOUNCE in ddgi_update.glsl): x the diffuse reflectance (unorm8 rgb,
// a 1 when the hit needs bounce light), y the normal and z the direction from
// the probe to the hit (octahedral, unorm16), w the distance (float bits).
layout(set = 0, binding = 40, std430) restrict writeonly buffer DDGIRayBounce {
	uvec4 ddgi_ray_bounce[];
};
#endif

#define DDGI_SAMPLER SAMPLER_LINEAR_CLAMP

#include "ddgi_sample_inc.glsl"
