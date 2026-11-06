extends Node
## Real RD shader selection versus independent exclusive-root CPU oracle.


func _ready() -> void:
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	var vertices := PackedVector3Array()
	var indices := PackedInt32Array()
	const EDGE := 48
	for z in EDGE + 1:
		for x in EDGE + 1:
			vertices.append(Vector3(x, sin(x * 0.2) * cos(z * 0.2), z))
	for z in EDGE:
		for x in EDGE:
			var a := z * (EDGE + 1) + x
			indices.append_array(PackedInt32Array([a, a + EDGE + 1, a + 1, a + 1, a + EDGE + 1, a + EDGE + 2]))
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	if mesh.generate_cluster_lods() != OK:
		_finish(["source hierarchy generation failed"])
		return
	RenderingServer.call_on_render_thread(_test_gpu.bind(mesh.cluster_data.get_surface_data(0)))


func _refine(surface: Dictionary, group: int, camera: Vector3, threshold: float, orthographic: bool) -> bool:
	var records: PackedInt32Array = surface.groups
	if records[group * 5 + 3] == 0:
		return false
	var bounds: PackedVector3Array = surface.bounds
	var center := bounds[group * 2] + bounds[group * 2 + 1] * 0.5
	var depth := 1.0 if orthographic else maxf(center.z - camera.z - bounds[group * 2 + 1].z * 0.5, 0.1)
	return surface.errors[group] * 500.0 / depth > threshold


func _oracle(surface: Dictionary, camera: Vector3, threshold: float, orthographic: bool, clip_x: float) -> Array[int]:
	var selected: Array[int] = []
	var pending: Array[int] = []
	for root in surface.roots:
		pending.append(root)
	var records: PackedInt32Array = surface.groups
	while not pending.is_empty():
		var group: int = pending.pop_back()
		if _refine(surface, group, camera, threshold, orthographic):
			for c in records[group * 5 + 3]:
				pending.append(surface.children[records[group * 5 + 2] + c])
		elif surface.bounds[group * 2].x <= clip_x:
			selected.append(group)
	selected.sort()
	return selected


func _encode_inputs(cases: Array[Dictionary], group_count: int) -> PackedByteArray:
	var inputs := PackedByteArray()
	for draw in cases.size():
		var test := cases[draw]
		for plane in 6:
			inputs.append_array(PackedFloat32Array([1.0 if plane == 0 else 0.0, 0, 0, test.clip if plane == 0 else 1]).to_byte_array())
		var camera: Vector3 = test.camera
		inputs.append_array(PackedFloat32Array([camera.x, camera.y, camera.z, 500, 0, 0, 1, 0.1]).to_byte_array())
		var threshold_bits := PackedFloat32Array([test.threshold]).to_byte_array().decode_s32(0)
		inputs.append_array(PackedInt32Array([group_count, 16 + draw * group_count * 5, int(test.ortho), threshold_bits]).to_byte_array())
	return inputs


func _test_gpu(surface: Dictionary) -> void:
	var failures: Array[String] = []
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		_finish.call_deferred(["real RenderingDevice required"])
		return
	var path := ProjectSettings.globalize_path("res://../../servers/rendering/renderer_rd/shaders/forward_clustered/mesh_clod_select.glsl")
	var source := RDShaderSource.new()
	source.source_compute = FileAccess.get_file_as_string(path).replace("#[compute]", "").replace("#VERSION_DEFINES", "")
	var spirv := rd.shader_compile_spirv_from_source(source)
	if not spirv.compile_error_compute.is_empty():
		_finish.call_deferred([spirv.compile_error_compute])
		return
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)
	var records: PackedInt32Array = surface.groups
	var clusters: PackedInt32Array = surface.clusters
	var group_count := records.size() / 5
	var group_bytes := PackedByteArray()
	var ranges: Array[Vector2i] = []
	for group in group_count:
		var position: Vector3 = surface.bounds[group * 2]
		var size: Vector3 = surface.bounds[group * 2 + 1]
		group_bytes.append_array(PackedFloat32Array([position.x, position.y, position.z, surface.errors[group], size.x, size.y, size.z, 0]).to_byte_array())
		var first := records[group * 5]
		var last := first + records[group * 5 + 1] - 1
		var index_offset := clusters[first * 3]
		var index_count := clusters[last * 3] + clusters[last * 3 + 1] - index_offset
		ranges.append(Vector2i(index_offset, index_count))
		group_bytes.append_array(PackedInt32Array([index_offset, index_count, records[group * 5 + 4], records[group * 5 + 3]]).to_byte_array())
	var cases: Array[Dictionary] = []
	for orthographic in [false, true]:
		for threshold in [0.0, 1.0, 1000000.0]:
			cases.append({"camera": Vector3(24, 10, -20), "threshold": threshold, "ortho": orthographic, "clip": 10000.0})
	# Other views share geometry, not command output; include fully rejected view.
	cases.append({"camera": Vector3(24, 10, -2000), "threshold": 1.0, "ortho": false, "clip": 24.0})
	cases.append({"camera": Vector3(24, 10, -20), "threshold": 0.0, "ortho": false, "clip": -10.0})
	var inputs := _encode_inputs(cases, group_count)
	var guards := PackedInt32Array()
	guards.resize(32 + cases.size() * group_count * 5)
	guards.fill(-1)
	var buffers: Array[RID] = [
		rd.storage_buffer_create(group_bytes.size(), group_bytes),
		rd.storage_buffer_create(inputs.size(), inputs),
		rd.storage_buffer_create(guards.size() * 4, guards.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
	]
	var uniforms: Array[RDUniform] = []
	for binding in 3:
		var uniform := RDUniform.new()
		uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
		uniform.binding = binding
		uniform.add_id(buffers[binding])
		uniforms.append(uniform)
	var uniform_set := rd.uniform_set_create(uniforms, shader, 0)
	for round_id in 2:
		if round_id == 1:
			for test in cases:
				test.threshold = 1000000.0 if test.threshold == 0.0 else 0.0
			cases[0].clip = -10.0
			cases[7].clip = 10000.0
			inputs = _encode_inputs(cases, group_count)
			rd.buffer_update(buffers[1], 0, inputs.size(), inputs)
		# Repeat in reverse submission order without clearing the command buffer.
		var list := rd.compute_list_begin()
		rd.compute_list_bind_compute_pipeline(list, pipeline)
		rd.compute_list_bind_uniform_set(list, uniform_set, 0)
		for step in cases.size():
			var draw := step if round_id == 0 else cases.size() - step - 1
			rd.compute_list_set_push_constant(list, PackedInt32Array([draw]).to_byte_array(), 4)
			rd.compute_list_dispatch(list, (group_count + 63) / 64, 1, 1)
		rd.compute_list_end()
		var result := rd.buffer_get_data(buffers[2])
		for draw in cases.size():
			var test := cases[draw]
			var actual: Array[int] = []
			for group in group_count:
				var offset := (16 + draw * group_count * 5 + group * 5) * 4
				var count := result.decode_u32(offset + 4)
				if count == 1:
					actual.append(group)
				elif count != 0:
					failures.append("unbounded instance count")
				if result.decode_u32(offset) != ranges[group].y or result.decode_u32(offset + 8) != ranges[group].x or result.decode_u32(offset + 12) != 0 or result.decode_u32(offset + 16) != 0:
					failures.append("source-index command fields corrupted")
			if actual != _oracle(surface, test.camera, test.threshold, test.ortho, test.clip):
				failures.append("GPU cut differs from exclusive-root oracle: draw %d round %d" % [draw, round_id])
		for guard in 16:
			if result.decode_u32(guard * 4) != 0xffffffff or result.decode_u32((guards.size() - 1 - guard) * 4) != 0xffffffff:
				failures.append("GPU wrote outside command capacity")
	for resource in [uniform_set, pipeline, shader] + buffers:
		rd.free_rid(resource)
	_finish.call_deferred(failures)


func _finish(failures: Array) -> void:
	if failures.is_empty():
		print("CLOD_TEST_PASS: GPU hierarchy cuts, perspective/orthographic, per-view output, frustum rejection, bounded native commands, repeat submission")
	else:
		for failure in failures:
			printerr("CLOD_TEST_FAIL: ", failure)
	get_tree().quit(0 if failures.is_empty() else 1)
