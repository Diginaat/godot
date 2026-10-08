// Vertex displacement. Raster moves the surface; the path tracer only matches
// if it rebuilds geometry from the vertex shader.
// Tests: custom vertex() in the path tracer.
shader_type spatial;

uniform float amplitude = 0.25;
uniform vec3 color : source_color = vec3(0.2, 0.45, 0.9);

void vertex() {
	VERTEX.y += sin(VERTEX.x * 4.0) * amplitude;
}

void fragment() {
	ALBEDO = color;
	ROUGHNESS = 0.35;
}
