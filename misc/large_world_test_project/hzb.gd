extends Node
## Real GPU min-depth pyramid and guarded indirect visibility checks.


func _ready() -> void:
	RenderingServer.call_on_render_thread(_test_gpu)


func _uniform(binding: int, type: int, resources: Array[RID]) -> RDUniform:
	var uniform := RDUniform.new()
	uniform.binding = binding
	uniform.uniform_type = type
	for resource in resources:
		uniform.add_id(resource)
	return uniform


func _test_gpu() -> void:
	var failures: Array[String] = []
	var rd := RenderingServer.get_rendering_device()
	var shaders: Array[RID] = []
	var pipelines: Array[RID] = []
	for variant in ["copy", "reduce", "occlude", "split"]:
		var path := "res://../../servers/rendering/renderer_rd/shaders/forward_clustered/mesh_hiz_%s.glsl" % ("occlude" if variant == "occlude" or variant == "split" else "reduce")
		var defines := {"copy": "#define MODE_COPY", "split": "#define MODE_SPLIT"}
		var source := RDShaderSource.new()
		# The engine's shader builder resolves #include; this compiler does not.
		var include := FileAccess.get_file_as_string(ProjectSettings.globalize_path(path.get_base_dir().path_join("mesh_hiz_inc.glsl")))
		source.source_compute = FileAccess.get_file_as_string(ProjectSettings.globalize_path(path)).replace("#[compute]", "").replace("#VERSION_DEFINES", defines.get(variant, "")).replace("#include \"mesh_hiz_inc.glsl\"", include)
		var spirv := rd.shader_compile_spirv_from_source(source)
		if not spirv.compile_error_compute.is_empty():
			for resource in pipelines + shaders:
				rd.free_rid(resource)
			_finish.call_deferred([spirv.compile_error_compute])
			return
		var shader := rd.shader_create_from_spirv(spirv)
		shaders.append(shader)
		pipelines.append(rd.compute_pipeline_create(shader))
	const WIDTH := 13
	const HEIGHT := 9
	const PADDED := 16
	const LEVELS := 5
	var format := RDTextureFormat.new()
	format.width = WIDTH
	format.height = HEIGHT
	format.format = RenderingDevice.DATA_FORMAT_R32_SFLOAT
	format.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
	var source_depth := rd.texture_create(format, RDTextureView.new())
	format.width = PADDED
	format.height = PADDED
	format.mipmaps = LEVELS
	format.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_STORAGE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
	var pyramid := rd.texture_create(format, RDTextureView.new())
	var views: Array[RID] = []
	for level in LEVELS:
		views.append(rd.texture_create_shared_from_slice(RDTextureView.new(), pyramid, 0, level))
	var sampler_state := RDSamplerState.new()
	sampler_state.max_lod = 100
	var sampler := rd.sampler_create(sampler_state)
	var boxes: Array[AABB] = [
		AABB(Vector3(-0.1, -0.1, 0.2), Vector3(0.2, 0.2, 0.1)), # Fully hidden.
		AABB(Vector3(-0.1, -0.1, 0.85), Vector3(0.2, 0.2, 0.05)), # In front.
		AABB(Vector3(-0.1, -0.1, 0.7), Vector3(0.2, 0.2, 0.1)), # Touching depth.
		AABB(Vector3(-0.1, -0.1, -0.1), Vector3(0.2, 0.2, 0.2)), # Clip crossing.
		AABB(Vector3(0.95, -0.1, 0.2), Vector3(0.2, 0.2, 0.1)), # Outside viewport.
		AABB(Vector3(-0.1, -0.1, 0.2), Vector3(0.2, 0.2, 0.1)), # Already unselected.
		AABB(Vector3(0.8, 0.7, 0.2), Vector3(0.1, 0.1, 0.1)), # Odd-edge padding.
	]
	var group_bytes := PackedByteArray()
	var commands := PackedInt32Array()
	commands.resize(32 + boxes.size() * 5)
	commands.fill(-1)
	for group in boxes.size():
		var box := boxes[group]
		group_bytes.append_array(PackedFloat32Array([box.position.x, box.position.y, box.position.z, 0, box.size.x, box.size.y, box.size.z, 0]).to_byte_array())
		group_bytes.append_array(PackedInt32Array([group * 3, 3, -1, 0]).to_byte_array())
		var offset := 16 + group * 5
		commands[offset] = 3
		commands[offset + 1] = 0 if group == 5 else 1
		commands[offset + 2] = group * 3
		commands[offset + 3] = 0
		commands[offset + 4] = 0
	var input := PackedFloat32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]).to_byte_array()
	input.append_array(PackedFloat32Array([0, 0, 0, 0]).to_byte_array())
	input.append_array(PackedInt32Array([boxes.size(), 16, WIDTH, HEIGHT]).to_byte_array())
	input.append_array(PackedInt32Array([2, 0, 0, 0]).to_byte_array()) # History offset 2.
	var history := PackedInt32Array()
	history.resize(2 + boxes.size() + 2)
	history.fill(0)
	var pass_init := PackedInt32Array()
	pass_init.resize(commands.size())
	pass_init.fill(-1)
	var buffers: Array[RID] = [rd.storage_buffer_create(group_bytes.size(), group_bytes), rd.storage_buffer_create(input.size(), input), rd.storage_buffer_create(commands.size() * 4, commands.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
			rd.storage_buffer_create(history.size() * 4, history.to_byte_array()), rd.storage_buffer_create(pass_init.size() * 4, pass_init.to_byte_array()), rd.storage_buffer_create(pass_init.size() * 4, pass_init.to_byte_array())]
	var sets: Array[RID] = []
	for level in LEVELS:
		var uniforms: Array[RDUniform] = [
			_uniform(0, RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, [sampler, source_depth if level == 0 else views[level - 1]]),
			_uniform(1, RenderingDevice.UNIFORM_TYPE_IMAGE, [views[level]]),
		]
		sets.append(rd.uniform_set_create(uniforms, shaders[0 if level == 0 else 1], 0))
	var occlude_uniforms: Array[RDUniform] = []
	for binding in 3:
		occlude_uniforms.append(_uniform(binding, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[binding]]))
	occlude_uniforms.append(_uniform(3, RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, [sampler, pyramid]))
	occlude_uniforms.append(_uniform(5, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[3]]))
	occlude_uniforms.append(_uniform(6, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[5]])) # Phase-2 commands.
	sets.append(rd.uniform_set_create(occlude_uniforms, shaders[2], 0))
	var split_uniforms: Array[RDUniform] = []
	for binding in 3:
		split_uniforms.append(_uniform(binding, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[binding]]))
	split_uniforms.append(_uniform(5, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[3]]))
	split_uniforms.append(_uniform(6, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[4]])) # Phase-1 depth commands.
	sets.append(rd.uniform_set_create(split_uniforms, shaders[3], 0))
	# Two-pass expectations per round (groups 0..6). Group 0 is hidden except in
	# round 1 (hole) and round 3 (uncertain bounds); group 5 is unselected.
	var history_before := [0, 0, 0, 0, 0, 0, 0]
	for round_id in 4:
		var depths := PackedFloat32Array()
		depths.resize(WIDTH * HEIGHT)
		depths.fill(0.8)
		if round_id == 1:
			depths[4 * WIDTH + 6] = 0.0 # A one-pixel visibility hole must recover.
		rd.texture_update(source_depth, 0, depths.to_byte_array())
		if round_id == 3:
			var uncertain := input.duplicate()
			for axis in 4:
				uncertain.encode_float(64 + axis * 4, 1.0)
			rd.buffer_update(buffers[1], 0, uncertain.size(), uncertain)
		rd.buffer_update(buffers[2], 0, commands.size() * 4, commands.to_byte_array())
		var list := rd.compute_list_begin()
		# Phase-1 split against the history left by the previous round.
		rd.compute_list_bind_compute_pipeline(list, pipelines[3])
		rd.compute_list_bind_uniform_set(list, sets[LEVELS + 1], 0)
		rd.compute_list_set_push_constant(list, PackedInt32Array([0, 1, 0, 0]).to_byte_array(), 16)
		rd.compute_list_dispatch(list, 1, 1, 1)
		rd.compute_list_add_barrier(list)
		for level in LEVELS:
			var size := PADDED >> level
			var source_width := WIDTH if level == 0 else size * 2
			var source_height := HEIGHT if level == 0 else size * 2
			rd.compute_list_bind_compute_pipeline(list, pipelines[0 if level == 0 else 1])
			rd.compute_list_bind_uniform_set(list, sets[level], 0)
			rd.compute_list_set_push_constant(list, PackedInt32Array([source_width, source_height, size, size]).to_byte_array(), 16)
			rd.compute_list_dispatch(list, (size + 7) / 8, (size + 7) / 8, 1)
			rd.compute_list_add_barrier(list)
		rd.compute_list_bind_compute_pipeline(list, pipelines[2])
		rd.compute_list_bind_uniform_set(list, sets[LEVELS], 0)
		rd.compute_list_set_push_constant(list, PackedInt32Array([0, 1, 0, 0]).to_byte_array(), 16)
		rd.compute_list_dispatch(list, 1, 1, 1)
		rd.compute_list_end()
		# Verify every mip against the exact padded min-depth CPU reduction.
		var expected := PackedFloat32Array()
		expected.resize(PADDED * PADDED)
		for y in HEIGHT:
			for x in WIDTH:
				expected[y * PADDED + x] = depths[y * WIDTH + x]
		var mip_data := rd.texture_get_data(pyramid, 0).to_float32_array()
		var mip_offset := 0
		for level in LEVELS:
			var size := PADDED >> level
			var actual := mip_data.slice(mip_offset, mip_offset + size * size)
			mip_offset += size * size
			if actual != expected:
				failures.append("reverse-Z farthest-depth mip %d differs: actual=%d expected=%d first=%s/%s" % [level, actual.size(), expected.size(), actual.slice(0, 4), expected.slice(0, 4)])
			if size > 1:
				var next := PackedFloat32Array()
				next.resize(size * size / 4)
				for y in size / 2:
					for x in size / 2:
						next[y * (size / 2) + x] = minf(minf(expected[y * 2 * size + x * 2], expected[y * 2 * size + x * 2 + 1]), minf(expected[(y * 2 + 1) * size + x * 2], expected[(y * 2 + 1) * size + x * 2 + 1]))
				expected = next
		var result := rd.buffer_get_data(buffers[2]).to_int32_array()
		var wanted := [1 if round_id == 1 or round_id == 3 else 0, 1, 1, 1, 1, 0, 1]
		for group in boxes.size():
			var offset := 16 + group * 5
			if result[offset + 1] != wanted[group]:
				failures.append("wrong conservative HZB visibility: group %d round %d" % [group, round_id])
			for word in [0, 2, 3, 4]:
				if result[offset + word] != commands[offset + word]:
					failures.append("HZB changed source geometry command fields")
		for guard in 16:
			if result[guard] != -1 or result[result.size() - guard - 1] != -1:
				failures.append("HZB wrote outside command capacity")
		# Two passes: phase 1 leaves out selected groups occluded when last tested,
		# phase 2 draws those of them that are visible now, history follows the test.
		var depth_commands := rd.buffer_get_data(buffers[4]).to_int32_array()
		var phase2 := rd.buffer_get_data(buffers[5]).to_int32_array()
		var history_after := rd.buffer_get_data(buffers[3]).to_int32_array()
		for group in boxes.size():
			var offset := 16 + group * 5
			var selected := group != 5
			var deferred: bool = selected and history_before[group] != 0
			var depth_wanted: int = 1 if selected and not deferred else 0
			var phase2_wanted: int = 1 if deferred and wanted[group] == 1 else 0
			var history_wanted: int = history_before[group] if not selected else (0 if wanted[group] == 1 else 1)
			if depth_commands[offset + 1] != depth_wanted or phase2[offset + 1] != phase2_wanted or history_after[2 + group] != history_wanted:
				failures.append("two-pass round %d group %d: depth %d (want %d), phase 2 %d (want %d), history %d (want %d)" % [round_id, group, depth_commands[offset + 1], depth_wanted, phase2[offset + 1], phase2_wanted, history_after[2 + group], history_wanted])
			for word in [0, 2, 3, 4]:
				if depth_commands[offset + word] != commands[offset + word] or phase2[offset + word] != commands[offset + word]:
					failures.append("two-pass commands changed source geometry fields")
			history_before[group] = history_after[2 + group]
		for guard in 16:
			if depth_commands[guard] != -1 or phase2[guard] != -1 or history_after[0] != 0 or history_after[1] != 0 or history_after[history_after.size() - 1] != 0:
				failures.append("two-pass kernels wrote outside their ranges")
	for resource in sets + pipelines + shaders + buffers + views + [pyramid, source_depth, sampler]:
		rd.free_rid(resource)
	_finish.call_deferred(failures)


func _finish(failures: Array) -> void:
	if failures.is_empty():
		print("HZB_TEST_PASS: real GPU padded min-depth mips, bounded occlusion, touching/near/edge protection, one-pixel-hole recovery and two-pass split/recovery/history")
	else:
		for failure in failures:
			printerr("HZB_TEST_FAIL: ", failure)
	get_tree().quit(0 if failures.is_empty() else 1)
