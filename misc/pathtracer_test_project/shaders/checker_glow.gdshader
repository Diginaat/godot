// Procedural albedo, roughness and an animated emissive stripe.
// Tests: custom fragment(), TIME, EMISSION from a ShaderMaterial.
shader_type spatial;

uniform vec3 color_a : source_color = vec3(0.85);
uniform vec3 color_b : source_color = vec3(0.08);
uniform float scale = 8.0;
uniform vec3 glow_color : source_color = vec3(1.0, 0.45, 0.1);
uniform float glow_energy = 6.0;

void fragment() {
	vec2 cell = floor(UV * scale);
	float k = mod(cell.x + cell.y, 2.0);
	ALBEDO = mix(color_a, color_b, k);
	ROUGHNESS = mix(0.15, 0.8, k);
	METALLIC = 0.0;
	float stripe = step(0.9, fract(UV.x * scale));
	EMISSION = glow_color * glow_energy * stripe * (0.5 + 0.5 * sin(TIME * 2.0));
}
