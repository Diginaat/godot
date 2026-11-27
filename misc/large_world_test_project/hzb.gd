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
	for variant in ["first", "reduce", "occlude", "split"]:
		var path := "res://../../servers/rendering/renderer_rd/shaders/forward_clustered/mesh_hiz_%s.glsl" % ("occlude" if variant == "occlude" or variant == "split" else "reduce")
		var defines := {"first": "#define MODE_FIRST", "split": "#define MODE_SPLIT"}
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
	# The pyramid starts at half the depth's resolution (mesh_hiz_inc.glsl):
	# 13 x 9 pixels pad to 16 x 16, so mip 0 is 8 x 8.
	const PADDED := 8
	const LEVELS := 4
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
	# Selection words (mesh_clod_select.glsl): the first index of each selected
	# group, UINT32_MAX (-1) for group 5. The surface's commands start at word
	# 20, so its selection words start at 20 / 5.
	const FIRST := 20
	var group_bytes := PackedByteArray()
	var selection := PackedInt32Array()
	selection.resize(FIRST / 5 + boxes.size() + 4)
	selection.fill(-1)
	for group in boxes.size():
		var box := boxes[group]
		group_bytes.append_array(PackedFloat32Array([box.position.x, box.position.y, box.position.z, 0, box.size.x, box.size.y, box.size.z, 0]).to_byte_array())
		group_bytes.append_array(PackedInt32Array([group * 3, 3, -1, 0]).to_byte_array())
		if group != 5:
			selection[FIRST / 5 + group] = group * 3
	var input := PackedFloat32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]).to_byte_array()
	input.append_array(PackedFloat32Array([0, 0, 0, 0]).to_byte_array())
	input.append_array(PackedInt32Array([boxes.size(), FIRST, WIDTH, HEIGHT]).to_byte_array())
	input.append_array(PackedInt32Array([2, 0, 0, 0]).to_byte_array()) # History offset 2, draw (count slot) 0.
	var uncertain := input.duplicate()
	for axis in 4:
		uncertain.encode_float(64 + axis * 4, 1.0)
	var history := PackedInt32Array()
	history.resize(2 + boxes.size() + 2)
	history.fill(0)
	# Colour, phase-1 and phase-2 streams, with 16 guard words at both ends.
	var stream_init := PackedInt32Array()
	stream_init.resize(FIRST * 2 + boxes.size() * 5)
	stream_init.fill(-1)
	var buffers: Array[RID] = [rd.storage_buffer_create(group_bytes.size(), group_bytes), rd.storage_buffer_create(input.size(), input), rd.storage_buffer_create(stream_init.size() * 4, stream_init.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
			rd.storage_buffer_create(history.size() * 4, history.to_byte_array()), rd.storage_buffer_create(stream_init.size() * 4, stream_init.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT), rd.storage_buffer_create(stream_init.size() * 4, stream_init.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
			rd.storage_buffer_create(selection.size() * 4, selection.to_byte_array()), rd.storage_buffer_create(4 * 4, PackedByteArray(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT)]
	const COLOR := 2
	const PHASE1 := 4
	const PHASE2 := 5
	const SELECTION := 6
	const COUNTS := 7
	var sets: Array[RID] = []
	for level in LEVELS:
		var uniforms: Array[RDUniform] = [
			_uniform(0, RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, [sampler, source_depth if level == 0 else views[level - 1]]),
			_uniform(1, RenderingDevice.UNIFORM_TYPE_IMAGE, [views[level]]),
		]
		sets.append(rd.uniform_set_create(uniforms, shaders[0 if level == 0 else 1], 0))
	var occlude_uniforms: Array[RDUniform] = [
		_uniform(0, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[0]]),
		_uniform(1, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[1]]),
		_uniform(2, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[SELECTION]]),
		_uniform(3, RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, [sampler, pyramid]),
		_uniform(5, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[3]]),
		_uniform(6, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[PHASE2]]),
		_uniform(7, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[COLOR]]),
		_uniform(8, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[COUNTS]]),
	]
	sets.append(rd.uniform_set_create(occlude_uniforms, shaders[2], 0))
	var split_uniforms: Array[RDUniform] = [
		_uniform(0, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[0]]),
		_uniform(1, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[1]]),
		_uniform(2, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[SELECTION]]),
		_uniform(5, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[3]]),
		_uniform(6, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[PHASE1]]),
		_uniform(8, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, [buffers[COUNTS]]),
	]
	sets.append(rd.uniform_set_create(split_uniforms, shaders[3], 0))
	# Two-pass expectations per round (groups 0..6). Group 0 is hidden except in
	# round 1 (hole) and round 3 (uncertain bounds); group 5 is unselected. The
	# four rounds run with one record per group, then with compact records.
	var history_before := [0, 0, 0, 0, 0, 0, 0]
	for pass_id in 8:
		var round_id := pass_id % 4
		var compact := pass_id / 4
		var depths := PackedFloat32Array()
		depths.resize(WIDTH * HEIGHT)
		depths.fill(0.8)
		if round_id == 1:
			depths[4 * WIDTH + 6] = 0.0 # A one-pixel visibility hole must recover.
		rd.texture_update(source_depth, 0, depths.to_byte_array())
		var round_input := uncertain if round_id == 3 else input
		rd.buffer_update(buffers[1], 0, round_input.size(), round_input)
		rd.buffer_clear(buffers[COUNTS], 0, 4 * 4)
		var list := rd.compute_list_begin()
		# Phase-1 split against the history left by the previous round.
		rd.compute_list_bind_compute_pipeline(list, pipelines[3])
		rd.compute_list_bind_uniform_set(list, sets[LEVELS + 1], 0)
		rd.compute_list_set_push_constant(list, PackedInt32Array([0, 1, compact, 1]).to_byte_array(), 16)
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
		rd.compute_list_set_push_constant(list, PackedInt32Array([0, 1, compact, 1]).to_byte_array(), 16)
		rd.compute_list_dispatch(list, 1, 1, 1)
		rd.compute_list_end()
		# Verify every mip against the exact padded min-depth CPU reduction:
		# mip 0 holds the farthest of each 2 x 2 pixels, clear depth (0) past
		# the image.
		var expected := PackedFloat32Array()
		expected.resize(PADDED * PADDED)
		for y in PADDED:
			for x in PADDED:
				var farthest := 1.0
				for sy in 2:
					for sx in 2:
						var px := x * 2 + sx
						var py := y * 2 + sy
						farthest = minf(farthest, depths[py * WIDTH + px] if px < WIDTH and py < HEIGHT else 0.0)
				expected[y * PADDED + x] = farthest
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
		# Per stream: the groups it must draw. Colour: selected and not proven
		# hidden. Phase 1: selected groups not occluded when last tested. Phase 2:
		# of the others, those visible now. History follows the test.
		var wanted := [1 if round_id == 1 or round_id == 3 else 0, 1, 1, 1, 1, 0, 1]
		var want_color: Array[int] = []
		var want_phase1: Array[int] = []
		var want_phase2: Array[int] = []
		for group in boxes.size():
			var selected := group != 5
			var deferred: bool = selected and history_before[group] != 0
			if selected and wanted[group] == 1:
				want_color.append(group)
			if selected and not deferred:
				want_phase1.append(group)
			if deferred and wanted[group] == 1:
				want_phase2.append(group)
		var counts := rd.buffer_get_data(buffers[COUNTS]).to_int32_array()
		var streams := {"colour": [COLOR, want_color, 1], "phase 1": [PHASE1, want_phase1, 2], "phase 2": [PHASE2, want_phase2, 3]}
		for stream_name in streams:
			var stream: Array = streams[stream_name]
			var data := rd.buffer_get_data(buffers[stream[0]]).to_int32_array()
			var drawn: Array[int] = []
			var records: int = boxes.size() if compact == 0 else counts[stream[2]]
			if records > boxes.size():
				failures.append("%s: compact count %d above the group count, round %d" % [stream_name, records, round_id])
				records = 0
			for record in records:
				var offset := FIRST + record * 5
				var group: int = record if compact == 0 else data[offset + 2] / 3
				var instances := data[offset + 1]
				if instances == 1:
					drawn.append(group)
				elif instances != 0 or compact == 1:
					failures.append("%s: unbounded instance count, round %d" % [stream_name, round_id])
				var first_wanted := group * 3 if instances == 1 else 0
				if data[offset] != 3 or data[offset + 2] != first_wanted or data[offset + 3] != 0 or data[offset + 4] != 0:
					failures.append("%s: source geometry fields changed, round %d compact %d" % [stream_name, round_id, compact])
			drawn.sort()
			if drawn != stream[1]:
				failures.append("%s round %d compact %d: drew %s, want %s" % [stream_name, round_id, compact, drawn, stream[1]])
			for guard in 16:
				if data[guard] != -1 or data[data.size() - guard - 1] != -1:
					failures.append("%s: kernel wrote outside command capacity" % stream_name)
		var history_after := rd.buffer_get_data(buffers[3]).to_int32_array()
		for group in boxes.size():
			var selected := group != 5
			var history_wanted: int = history_before[group] if not selected else (0 if wanted[group] == 1 else 1)
			if history_after[2 + group] != history_wanted:
				failures.append("round %d compact %d group %d: history %d (want %d)" % [round_id, compact, group, history_after[2 + group], history_wanted])
			history_before[group] = history_after[2 + group]
		if history_after[0] != 0 or history_after[1] != 0 or history_after[history_after.size() - 1] != 0:
			failures.append("two-pass kernels wrote outside the history range")
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
