// Cut-out pattern with a custom alpha scissor threshold.
// Tests: ALPHA plus ALPHA_SCISSOR_THRESHOLD in a ShaderMaterial (any-hit path).
shader_type spatial;
render_mode cull_disabled;

uniform vec3 leaf_color : source_color = vec3(0.2, 0.6, 0.15);
uniform float holes = 6.0;

void fragment() {
	vec2 p = fract(UV * holes) - 0.5;
	ALBEDO = leaf_color;
	ROUGHNESS = 0.6;
	ALPHA = step(0.3, length(p));
	ALPHA_SCISSOR_THRESHOLD = 0.5;
}
