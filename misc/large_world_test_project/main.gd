extends Node3D
## Real RenderingDevice tests, not a benchmark or a built-in visibility culler.
## The unsafe sentinel case is never attached to a rendering instance.

const CAPACITY := 8
const SURFACES := 2
const FINAL_GPU_COUNT := 3

var _mesh: ArrayMesh
var _multimesh := RID()
var _instance := RID()
var _camera: Camera3D


func _ready() -> void:
	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	_camera.size = 10.0
	_camera.position = Vector3(3.5, 0.5, 10.0)
	add_child(_camera)
	_camera.current = true
	_mesh = ArrayMesh.new()
	for surface in SURFACES:
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		var y := float(surface)
		arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([
			Vector3(-0.3, y - 0.25, 0), Vector3(0, y + 0.3, 0), Vector3(0.3, y - 0.25, 0)
		])
		if surface == 0:
			arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
		_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.albedo_color = Color.RED if surface == 0 else Color.GREEN
		_mesh.surface_set_material(surface, material)
	# Use only existing native RenderingServer APIs. No separate raster pipeline.
	_multimesh = RenderingServer.multimesh_create()
	RenderingServer.multimesh_set_mesh(_multimesh, _mesh.get_rid())
	RenderingServer.multimesh_allocate_data(_multimesh, CAPACITY, RenderingServer.MULTIMESH_TRANSFORM_3D, false, false, true)
	var transforms := PackedFloat32Array()
	for i in CAPACITY:
		transforms.append_array(PackedFloat32Array([1, 0, 0, float(i), 0, 1, 0, 0, 0, 0, 1, 0]))
	RenderingServer.multimesh_set_buffer(_multimesh, transforms)
	RenderingServer.multimesh_set_custom_aabb(_multimesh, AABB(Vector3(-1, -1, -1), Vector3(10, 4, 2)))
	await get_tree().process_frame
	RenderingServer.call_on_render_thread(_check_commands)


func _expect_commands(rd: RenderingDevice, command: RID, expected: int, label: String, failures: Array[String]) -> void:
	var bytes := rd.buffer_get_data(command)
	if bytes.size() != SURFACES * 20:
		failures.append("%s: command size %d, expected %d" % [label, bytes.size(), SURFACES * 20])
		return
	for surface in SURFACES:
		for word in 5:
			var wanted := 0
			if word == 0:
				wanted = 3
			elif word == 1:
				wanted = expected
			var actual := bytes.decode_u32(surface * 20 + word * 4)
			if actual != wanted:
				failures.append("%s: surface %d word %d = %d, expected %d" % [label, surface, word, actual, wanted])


func _check_commands() -> void:
	var failures: Array[String] = []
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		failures.append("Real RenderingDevice required; do not use --headless or Compatibility.")
		_checks_done.call_deferred(failures)
		return
	var command := RenderingServer.multimesh_get_command_buffer_rd_rid(_multimesh)
	_expect_commands(rd, command, 0, "initial GPU-owned count", failures)
	for count in [0, 2, CAPACITY, -1, 0, -1]:
		RenderingServer.multimesh_set_visible_instances(_multimesh, count)
		if RenderingServer.multimesh_get_visible_instances(_multimesh) != count:
			failures.append("CPU-visible count must retain public sentinel semantics")
		_expect_commands(rd, command, CAPACITY if count == -1 else count, "CPU count %d" % count, failures)
	if not failures.is_empty():
		# Never submit a draw if the command can contain the pre-fix 0xffffffff.
		_checks_done.call_deferred(failures)
		return
	# Test-only GPU producer. It changes only instanceCount, preserving indexed
	# and non-indexed command layouts. Synchronous reads below are assertions,
	# never proposed as production per-frame visibility feedback.
	var source := RDShaderSource.new()
	source.source_compute = """
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0, std430) buffer Commands { uint words[]; } commands;
layout(push_constant, std430) uniform Params {
	uint requested;
	uint capacity;
	uint surfaces;
	uint pad;
} params;
void main() {
	uint surface = gl_GlobalInvocationID.x;
	if (surface < params.surfaces) {
		commands.words[surface * 5u + 1u] = min(params.requested, params.capacity);
	}
}
"""
	var spirv := rd.shader_compile_spirv_from_source(source)
	if not spirv.compile_error_compute.is_empty():
		failures.append(spirv.compile_error_compute)
		_checks_done.call_deferred(failures)
		return
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)
	var uniform := RDUniform.new()
	uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	uniform.binding = 0
	uniform.add_id(command)
	var uniform_set := rd.uniform_set_create([uniform], shader, 0)
	for requested in [0, CAPACITY + 100, FINAL_GPU_COUNT]:
		var params := PackedInt32Array([requested, CAPACITY, SURFACES, 0]).to_byte_array()
		var list := rd.compute_list_begin()
		rd.compute_list_bind_compute_pipeline(list, pipeline)
		rd.compute_list_bind_uniform_set(list, uniform_set, 0)
		rd.compute_list_set_push_constant(list, params, params.size())
		rd.compute_list_dispatch(list, SURFACES, 1, 1)
		rd.compute_list_end()
		_expect_commands(rd, command, mini(requested, CAPACITY), "GPU count %d" % requested, failures)
	rd.free_rid(uniform_set)
	rd.free_rid(pipeline)
	rd.free_rid(shader)
	_checks_done.call_deferred(failures)


func _checks_done(failures: Array[String]) -> void:
	if not failures.is_empty():
		for failure in failures:
			printerr("INDIRECT_TEST_FAIL: ", failure)
		_finish(1)
		return
	_instance = RenderingServer.instance_create2(_multimesh, get_world_3d().scenario)
	for frame in 8:
		await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	for surface in SURFACES:
		for i in CAPACITY:
			var pixel := Vector2i(_camera.unproject_position(Vector3(i, surface, 0)))
			var color := image.get_pixelv(pixel)
			var wanted := Color.RED if surface == 0 else Color.GREEN
			if i >= FINAL_GPU_COUNT:
				wanted = Color.BLACK
			if Vector3(color.r - wanted.r, color.g - wanted.g, color.b - wanted.b).length() > 0.15:
				failures.append("surface %d instance %d pixel %s = %s, expected %s" % [surface, i, pixel, color, wanted])
	if failures.is_empty():
		print("INDIRECT_TEST_PASS: CPU sentinel, two surface layouts, bounded GPU counts, native material pixels")
	else:
		for failure in failures:
			printerr("INDIRECT_TEST_FAIL: ", failure)
	_finish(0 if failures.is_empty() else 1)


func _finish(code: int) -> void:
	if _instance.is_valid():
		RenderingServer.free_rid(_instance)
	if _multimesh.is_valid():
		RenderingServer.free_rid(_multimesh)
	get_tree().quit(code)
