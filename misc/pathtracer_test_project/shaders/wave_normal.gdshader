// Brushed metal with a procedural normal map.
// Tests: NORMAL_MAP and NORMAL_MAP_DEPTH written by a ShaderMaterial.
shader_type spatial;

uniform vec3 base_color : source_color = vec3(0.95, 0.75, 0.4);
uniform float frequency = 40.0;

void fragment() {
	ALBEDO = base_color;
	METALLIC = 1.0;
	ROUGHNESS = 0.25;
	float s = sin(UV.x * frequency) * 0.5;
	NORMAL_MAP = normalize(vec3(s, 0.0, 1.0)) * 0.5 + 0.5;
	NORMAL_MAP_DEPTH = 1.0;
}
