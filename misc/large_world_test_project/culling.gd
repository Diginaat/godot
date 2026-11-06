extends Node3D
## Native renderer A/B tests. No script-side visibility or draw submission.

signal profile_checked(found: bool)

const SETTING := "rendering/geometry/multimesh_gpu_culling/enabled"
const KERNEL_COUNT := 16641 # More than 256 scan blocks, plus a partial workgroup.
const KERNEL_STRIDE := ((KERNEL_COUNT + 63) / 64) * 64
var _failures: Array[String] = []
var _camera: Camera3D
var _mesh: ArrayMesh
var _nodes: Array[MultiMeshInstance3D] = []
var _secondary: SubViewport


func _ready() -> void:
	_camera = Camera3D.new()
	_camera.position = Vector3(0, 0, 12)
	_camera.size = 12
	add_child(_camera)
	_camera.current = true
	_secondary = SubViewport.new()
	_secondary.size = Vector2i(320, 180)
	_secondary.world_3d = get_world_3d()
	_secondary.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_secondary)
	var other_camera := Camera3D.new()
	other_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	other_camera.size = 6
	other_camera.position = Vector3(2, 2, 10)
	_secondary.add_child(other_camera)
	other_camera.current = true
	_mesh = ArrayMesh.new()
	for surface in 2:
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		var y := float(surface) * 0.3
		arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.15, y - 0.1, 0), Vector3(0, y + 0.15, 0), Vector3(0.15, y - 0.1, 0)])
		if surface == 0:
			arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
		_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var shader := Shader.new()
		shader.code = """shader_type spatial;
render_mode unshaded, cull_disabled;
varying flat vec3 tint;
int identity(int value) { return value; }
void vertex() { int original = identity(INSTANCE_ID); int previous = INSTANCE_ID - 1; tint = vec3(fract(float(previous + 1) * 0.173), INSTANCE_CUSTOM.x, COLOR.b); if (original < 0) { tint = vec3(0.0); } }
void fragment() { ALBEDO = tint; }
"""
		var material := ShaderMaterial.new()
		material.shader = shader
		_mesh.surface_set_material(surface, material)
	_replace_multimesh(65)
	RenderingServer.call_on_render_thread(_check_kernel)


func _replace_multimesh(count: int, coplanar: bool = false) -> void:
	for node in _nodes:
		node.queue_free()
	_nodes.clear()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.use_custom_data = true
	mm.instance_count = count
	mm.mesh = _mesh
	var data := PackedFloat32Array()
	data.resize(count * 20)
	for i in count:
		var x := float((i / 2) % 8) - 3.5 if i < 65 and i % 2 == 0 else 1000.0 + i
		var y := float(i / 16) * 0.6 - 2.0 if i < 65 else 0.0
		if coplanar and i == 4:
			x = -2.5
			y = -2.0 # Same geometry/depth as source ID 2, different color/custom data.
		var record := PackedFloat32Array([1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, 0, 1, 1, float(i % 7 + 1) / 8.0, 1, float(i % 11 + 1) / 12.0, 0, 0, 0])
		for word in 20:
			data[i * 20 + word] = record[word]
	mm.buffer = data
	for copy in 2:
		var node := MultiMeshInstance3D.new()
		node.multimesh = mm
		node.position.y = float(copy) * 3.5
		add_child(node)
		_nodes.append(node)


func _check_kernel() -> void:
	var rd := RenderingServer.get_rendering_device()
	var path := ProjectSettings.globalize_path("res://").path_join("../../servers/rendering/renderer_rd/shaders/forward_clustered/multimesh_cull.glsl").simplify_path()
	var shaders: Array[RID] = []
	var pipelines: Array[RID] = []
	for define in ["", "#define MODE_SCAN", "#define MODE_SCATTER"]:
		var source := RDShaderSource.new()
		source.source_compute = FileAccess.get_file_as_string(path).replace("#[compute]", "").replace("#VERSION_DEFINES", define)
		var spirv := rd.shader_compile_spirv_from_source(source)
		if not spirv.compile_error_compute.is_empty():
			for resource in pipelines + shaders:
				rd.free_rid(resource)
			_kernel_done.call_deferred([spirv.compile_error_compute])
			return
		var shader := rd.shader_create_from_spirv(spirv)
		shaders.append(shader)
		pipelines.append(rd.compute_pipeline_create(shader))
	# Two transform histories, five vec4s per input. Only current
	# history may be tested; every third current input is well outside the box.
	var transforms := PackedFloat32Array()
	var expected: Array[int] = []
	for history in 2:
		for i in KERNEL_COUNT:
			var x := 10.0 if history == 0 or i % 3 == 0 else 0.0
			if history == 1 and i == 1:
				x = 2.625 # Transformed AABB touches the right plane; retain it.
			if history == 1 and i == 2:
				x = 2.626 # Outside even with the conservative numerical margin.
			transforms.append_array(PackedFloat32Array([2, 0.5, 0, x, 0, -1, 0, 0, 0, 0, 0.5, 0, 1, 1, 1, 1, 0, 0, 0, 0]))
			if history == 1 and i % 3 != 0 and i != 2:
				expected.append(i)
	var inputs := PackedByteArray()
	var draw_counts := [65, 0, KERNEL_COUNT]
	for draw in 3:
		var record := PackedFloat32Array([1, 0, 0, 2, -1, 0, 0, 2, 0, 1, 0, 2, 0, -1, 0, 2, 0, 0, 1, 2, 0, 0, -1, 2, 0, 0, 0, 0, 0.25, 0.25, 0.25, 0]).to_byte_array()
		record.append_array(PackedInt32Array([draw_counts[draw], 5, KERNEL_COUNT, 64 + draw * KERNEL_STRIDE]).to_byte_array())
		# No GPU LOD: local camera and LOD edges unused; one level, command record = draw.
		record.append_array(PackedFloat32Array([0, 0, 0, 0, 0, 0, 0, 0]).to_byte_array())
		record.append_array(PackedInt32Array([1, KERNEL_STRIDE, draw, 0]).to_byte_array())
		inputs.append_array(record)
	var ids := PackedInt32Array()
	ids.resize(64 + 3 * KERNEL_STRIDE + 64)
	ids.fill(-1)
	var commands := PackedInt32Array([3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0])
	var buffers: Array[RID] = [
		rd.storage_buffer_create(transforms.size() * 4, transforms.to_byte_array()),
		rd.storage_buffer_create(inputs.size(), inputs),
		rd.storage_buffer_create(ids.size() * 4, ids.to_byte_array()),
		rd.storage_buffer_create(commands.size() * 4, commands.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
		rd.storage_buffer_create(ids.size() * 4),
		rd.storage_buffer_create((ids.size() / 64) * 4)
	]
	# Set 0: shared inputs, IDs, commands and scratch. Set 1 (cull mode only):
	# the MultiMesh transform buffer.
	var uniforms: Array[RDUniform] = []
	for binding in 5:
		var uniform := RDUniform.new()
		uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
		uniform.binding = binding
		uniform.add_id(buffers[binding + 1])
		uniforms.append(uniform)
	var transforms_uniform := RDUniform.new()
	transforms_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	transforms_uniform.binding = 0
	transforms_uniform.add_id(buffers[0])
	var uniform_sets: Array[RID] = []
	for shader in shaders:
		uniform_sets.append(rd.uniform_set_create(uniforms, shader, 0))
	var transforms_set := rd.uniform_set_create([transforms_uniform], shaders[0], 1)
	var list := rd.compute_list_begin()
	for mode in 3:
		rd.compute_list_bind_compute_pipeline(list, pipelines[mode])
		rd.compute_list_bind_uniform_set(list, uniform_sets[mode], 0)
		if mode == 0:
			rd.compute_list_bind_uniform_set(list, transforms_set, 1)
		for draw in 3:
			rd.compute_list_set_push_constant(list, PackedInt32Array([draw]).to_byte_array(), 4)
			rd.compute_list_dispatch(list, 1 if mode == 1 else maxi(1, (draw_counts[draw] + 63) / 64), 1, 1)
		if mode != 2:
			rd.compute_list_add_barrier(list)
	rd.compute_list_end()
	var result_commands := rd.buffer_get_data(buffers[3])
	var result_ids := rd.buffer_get_data(buffers[2])
	var failures: Array[String] = []
	for draw in 3:
		var wanted: Array[int] = []
		if draw != 1:
			for source_id in expected:
				if source_id < draw_counts[draw]:
					wanted.append(source_id)
		var actual_count := result_commands.decode_u32(draw * 20 + 4)
		if actual_count != wanted.size():
			failures.append("kernel draw %d: count %d, expected %d" % [draw, actual_count, wanted.size()])
		var actual: Array[int] = []
		for slot in mini(actual_count, draw_counts[draw]):
			actual.append(result_ids.decode_u32((64 + draw * KERNEL_STRIDE + slot) * 4))
		if actual != wanted:
			failures.append("kernel draw %d: source IDs missing, duplicated or out of original order" % draw)
		for slot in range(mini(actual_count, draw_counts[draw]), KERNEL_STRIDE):
			if result_ids.decode_u32((64 + draw * KERNEL_STRIDE + slot) * 4) != 0xffffffff:
				failures.append("kernel wrote beyond its visible count")
		for word in [0, 2, 3, 4]:
			if result_commands.decode_u32(draw * 20 + word * 4) != (3 if word == 0 else 0):
				failures.append("kernel modified geometry command fields")
	for guard in [0, 1, 2, 3, ids.size() - 4, ids.size() - 3, ids.size() - 2, ids.size() - 1]:
		if result_ids.decode_u32(guard * 4) != 0xffffffff:
			failures.append("kernel output guard overwritten")
	for resource in uniform_sets + [transforms_set] + pipelines + shaders:
		rd.free_rid(resource)
	for buffer in buffers:
		rd.free_rid(buffer)
	_kernel_done.call_deferred(failures)


func _image(enabled: bool) -> Image:
	ProjectSettings.set_setting(SETTING, enabled)
	for frame in 16:
		await RenderingServer.frame_post_draw
	if enabled:
		RenderingServer.call_on_render_thread(_read_profile)
		if not await profile_checked:
			_failures.append("native culler was not dispatched for both viewports")
	return get_viewport().get_texture().get_image()


func _read_profile() -> void:
	var rd := RenderingServer.get_rendering_device()
	var count := 0
	for i in rd.get_captured_timestamps_count():
		if rd.get_captured_timestamp_name(i) == "MultiMesh Frustum Culling":
			count += 1
	profile_checked.emit.call_deferred(count >= 2)


func _compare(label: String) -> void:
	var reference := await _image(false)
	var secondary_reference := _secondary.get_texture().get_image()
	var culled := await _image(true)
	var secondary_culled := _secondary.get_texture().get_image()
	if secondary_reference.get_data() != secondary_culled.get_data():
		_failures.append("secondary viewport pixels differ: " + label)
	if reference.get_data() != culled.get_data():
		_failures.append("native renderer pixels differ: " + label)
	var coloured := false
	for y in range(0, reference.get_height(), 2):
		for x in range(0, reference.get_width(), 2):
			var pixel := reference.get_pixel(x, y)
			if pixel.r + pixel.g + pixel.b > 0.2:
				coloured = true
	if not coloured:
		_failures.append("empty reference image: " + label)


func _kernel_done(failures: Array) -> void:
	for failure in failures:
		_failures.append(str(failure))
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	await _compare("orthogonal, source IDs, colors, custom data, two shared-resource instances")
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.position.x = 2.0
	_secondary.size = Vector2i(384, 216)
	await _compare("perspective camera movement")
	_replace_multimesh(100000, true)
	await _compare("100k potential instances, including coplanar colored IDs")
	ProjectSettings.set_setting(SETTING, false)
	if _failures.is_empty():
		print("CULLING_TEST_PASS: native kernel rejection, source IDs, history offsets, guards, native raster A/B at 65 and 100k instances")
	else:
		for failure in _failures:
			printerr("CULLING_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
