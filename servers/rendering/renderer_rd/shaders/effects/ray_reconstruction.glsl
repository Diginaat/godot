#[compute]

#version 450

#VERSION_DEFINES

// Native ray reconstruction (path tracer denoiser). See
// docs/renderer/native_ray_reconstruction.md.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	uint debug_mode;
	uint pad;
}
params;

#ifdef MODE_COMPOSE

// Path tracer outputs: clean part (emission, fog, sky), diffuse and specular
// radiance, guides.
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2D base_image;
layout(rgba16f, set = 0, binding = 1) uniform restrict readonly image2D diffuse_image;
layout(rgba16f, set = 0, binding = 2) uniform restrict readonly image2D specular_image;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D output_image;

#define DEBUG_NONE 0u
#define DEBUG_BASE 1u
#define DEBUG_DIFFUSE 2u
#define DEBUG_SPECULAR 3u

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	vec3 base = imageLoad(base_image, pos).rgb;
	vec3 diffuse = imageLoad(diffuse_image, pos).rgb;
	vec3 specular = imageLoad(specular_image, pos).rgb;

	vec3 color = base + diffuse + specular;
	if (params.debug_mode == DEBUG_BASE) {
		color = base;
	} else if (params.debug_mode == DEBUG_DIFFUSE) {
		color = diffuse;
	} else if (params.debug_mode == DEBUG_SPECULAR) {
		color = specular;
	}
	imageStore(output_image, pos, vec4(color, 1.0));
}

#endif // MODE_COMPOSE
