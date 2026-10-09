// Dynamic diffuse global illumination (DDGI): shared data layout and helpers.
//
// An independent implementation of ray traced irradiance probes, after
// Majercik et al., "Dynamic Diffuse Global Illumination with Ray-Traced
// Irradiance Fields" (JCGT 2019) and "Scaling Probe-Based Real-Time Dynamic
// Global Illumination for Production" (JCGT 2021).
//
// Probes sit on regular grids ("volumes"). Each probe stores, in two texture
// atlases, an octahedral map of irradiance (stored as the cosine weighted mean
// radiance, so a Lambertian surface reflects albedo * value) and an octahedral
// map of the mean and squared mean distance to the nearest surface (for a
// Chebyshev visibility test).
//
// This file has the data layout and pure helpers. ddgi_sample_inc.glsl has
// the functions that read the DDGI resources. Requires oct_inc.glsl.

#define DDGI_MAX_VOLUMES 8

#define DDGI_PROBE_NEW 0u // Never updated: skipped when sampling, updated first.
#define DDGI_PROBE_ACTIVE 1u
#define DDGI_PROBE_INACTIVE 2u // No surface nearby: traced with the fixed rays only.
#define DDGI_PROBE_INSIDE 3u // Inside geometry: never sampled.
// Brought into the volume by scrolling, not traced yet. Sampled with the
// atlas texels left by the probe that scrolled out (stale, but better than
// nothing); the first update replaces them instead of blending with them.
#define DDGI_PROBE_SCROLLED 4u

#define DDGI_VOLUME_FLAG_RELOCATION 1
#define DDGI_VOLUME_FLAG_CLASSIFICATION 2

// Ray data stores the signed hit distance; misses use this value.
#define DDGI_MISS_DISTANCE 60000.0

struct DDGIVolume {
	vec4 world_to_local[3]; // Rows of a 3x4 affine transform (volume center at the origin).
	vec4 local_to_world[3];
	ivec4 grid; // xyz: probes per axis, w: global index of the first probe.
	ivec4 scroll; // xyz: storage offset of logical probe (0,0,0), w: DDGI_VOLUME_FLAG_*.
	vec4 spacing; // x: probe spacing, y: normal bias, z: view bias (world units), w: max ray distance.
	vec4 params; // x: energy, y: hysteresis, z: update rate weight, w: edge blend width (in probes).
};

struct DDGIProbe {
	vec3 offset; // Relocation offset in volume local space (world units).
	uint state; // DDGI_PROBE_*.
	float urgency; // Scheduler credit; the probe is traced when it reaches 1.
	float variability; // Moving average of the relative change per update.
	uint last_update_frame;
	float pending_change; // Signed relative change of the last update above the noise, not applied yet.
};

struct DDGIDataBlock {
	DDGIVolume volumes[DDGI_MAX_VOLUMES];
	ivec4 volume_reset[DDGI_MAX_VOLUMES]; // xyz: probes scrolled this frame, w: 1 resets the whole volume.
	vec4 ray_rotation[3]; // Rows of this frame's random rotation for probe rays.
	mat4 camera_view_projection;
	vec4 camera_position; // xyz: camera, w: unused.
	uvec4 counts; // x: volume count, y: rays per probe, z: fixed rays per probe, w: update capacity.
	uvec4 atlas; // x: irradiance texels, y: distance texels, z: probes per atlas row, w: frame.
	vec4 atlas_inv_size; // xy: irradiance atlas, zw: distance atlas.
	vec4 schedule; // x: base update rate, y: total probes, z: max radiance per ray, w: bounce energy.
	vec4 miss_color; // rgb: radiance of rays that miss when there is no sky, w: 1 = use the sky.
};

vec3 ddgi_xform(vec4 rows[3], vec3 p) {
	return vec3(dot(rows[0].xyz, p) + rows[0].w, dot(rows[1].xyz, p) + rows[1].w, dot(rows[2].xyz, p) + rows[2].w);
}

vec3 ddgi_xform_dir(vec4 rows[3], vec3 d) {
	return vec3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}

ivec3 ddgi_imod(ivec3 a, ivec3 n) {
	return ((a % n) + n) % n;
}

// Logical probe coordinates (0..grid-1 from the volume's min corner) to the
// global probe index. Scrolling volumes rotate storage instead of moving data.
// Both the coordinates and the scroll offset are in 0..grid-1, so a
// conditional subtraction replaces the (slow) integer modulo.
uint ddgi_probe_index(DDGIVolume vol, ivec3 logical) {
	ivec3 s = logical + vol.scroll.xyz;
	s -= ivec3(greaterThanEqual(s, vol.grid.xyz)) * vol.grid.xyz;
	return uint(vol.grid.w + s.x + s.y * vol.grid.x + s.z * vol.grid.x * vol.grid.y);
}

ivec3 ddgi_probe_logical(DDGIVolume vol, uint p_probe) {
	int rel = int(p_probe) - vol.grid.w;
	ivec3 s = ivec3(rel % vol.grid.x, (rel / vol.grid.x) % vol.grid.y, rel / (vol.grid.x * vol.grid.y));
	return ddgi_imod(s - vol.scroll.xyz, vol.grid.xyz);
}

vec3 ddgi_probe_local_position(DDGIVolume vol, ivec3 logical) {
	return (vec3(logical) - (vec3(vol.grid.xyz) - 1.0) * 0.5) * vol.spacing.x;
}

vec3 ddgi_probe_world_position(DDGIVolume vol, ivec3 logical, vec3 offset) {
	return ddgi_xform(vol.local_to_world, ddgi_probe_local_position(vol, logical) + offset);
}

// Top-left texel of the probe's tile interior. The probes per atlas row are
// a power of two, so this needs no integer division.
ivec2 ddgi_tile_origin(uint p_probe, uint p_texels, uint p_probes_per_row) {
	uint shift = uint(findMSB(p_probes_per_row));
	return ivec2(p_probe & (p_probes_per_row - 1u), p_probe >> shift) * int(p_texels + 2u) + 1;
}

vec2 ddgi_atlas_uv(uint p_probe, vec3 p_dir, uint p_texels, uint p_probes_per_row, vec2 p_inv_size) {
	vec2 origin = vec2(ddgi_tile_origin(p_probe, p_texels, p_probes_per_row));
	return (origin + vec3_to_oct(p_dir) * float(p_texels)) * p_inv_size;
}

// Direction at the center of interior texel p_texel of an octahedral tile.
vec3 ddgi_texel_direction(ivec2 p_texel, uint p_texels) {
	vec2 uv = (vec2(p_texel) + 0.5) / float(p_texels);
	return oct_to_vec3(uv * 2.0 - 1.0);
}

// Interior texel that a border texel of an octahedral tile copies. p_texel is
// relative to the interior (-1 and p_texels are the border).
ivec2 ddgi_border_source(ivec2 p_texel, int p_texels) {
	int n = p_texels;
	ivec2 t = p_texel;
	bool left = t.x < 0, right = t.x >= n, top = t.y < 0, bottom = t.y >= n;
	if ((left || right) && (top || bottom)) {
		// Corners copy the diagonally opposite corner.
		return ivec2(left ? n - 1 : 0, top ? n - 1 : 0);
	}
	if (top || bottom) {
		return ivec2(n - 1 - t.x, top ? 0 : n - 1);
	}
	return ivec2(left ? 0 : n - 1, n - 1 - t.y);
}

vec3 ddgi_spherical_fibonacci(uint p_index, uint p_count) {
	const float golden_ratio_conjugate = 0.61803398875;
	float phi = 2.0 * 3.14159265359 * fract(float(p_index) * golden_ratio_conjugate);
	float cos_theta = 1.0 - (2.0 * float(p_index) + 1.0) / float(p_count);
	float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0, 1.0));
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

float ddgi_luminance(vec3 c) {
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}
