extends "res://clod.gd"
## Reuses real cooked grid; tests a proposed residency snapshot, not a page pool.


func _forest(source: Dictionary) -> Dictionary:
	var result := source.duplicate(true)
	var groups: PackedInt32Array = source.groups.duplicate()
	var clusters: PackedInt32Array = source.clusters.duplicate()
	var children: PackedInt32Array = source.children.duplicate()
	var roots: PackedInt32Array = source.roots.duplicate()
	var indices: PackedInt32Array = source.indices.duplicate()
	var count := groups.size() / 5
	for group in count:
		groups[group * 5] += source.clusters.size() / 3
		groups[group * 5 + 2] += source.children.size()
		if groups[group * 5 + 4] != -1:
			groups[group * 5 + 4] += count
	for cluster in clusters.size() / 3:
		clusters[cluster * 3] += source.indices.size()
		clusters[cluster * 3 + 2] += count
	for i in children.size():
		children[i] += count
	for i in roots.size():
		roots[i] += count
	for i in indices.size():
		indices[i] += source.vertex_count
	for key in ["groups", "clusters", "children", "roots", "indices", "bounds", "cluster_bounds", "errors"]:
		var duplicate: Variant = source[key].duplicate()
		match key:
			"groups": duplicate.append_array(groups)
			"clusters": duplicate.append_array(clusters)
			"children": duplicate.append_array(children)
			"roots": duplicate.append_array(roots)
			"indices": duplicate.append_array(indices)
			_: duplicate.append_array(source[key])
		result[key] = duplicate
	result.vertex_count *= 2
	result.source_index_count *= 2
	result.erase("source_signature") # Synthetic two-root metadata, not a source attachment.
	return result


func _covered(surface: Dictionary, mask: PackedByteArray, group: int) -> bool:
	if mask[group] != 0:
		return true
	var groups: PackedInt32Array = surface.groups
	if groups[group * 5 + 3] == 0:
		return false
	for c in groups[group * 5 + 3]:
		if not _covered(surface, mask, surface.children[groups[group * 5 + 2] + c]):
			return false
	return true


func _resident_oracle(surface: Dictionary, mask: PackedByteArray, threshold: float) -> Dictionary:
	var selected: Array[int] = []
	var requests := PackedInt32Array()
	requests.resize(mask.size() + 1)
	var pending: Array[int] = []
	for root in surface.roots:
		if not _covered(surface, mask, root):
			requests[0] = 1
		pending.append(root)
	if requests[0] != 0:
		return {"selected": selected, "requests": requests}
	var groups: PackedInt32Array = surface.groups
	while not pending.is_empty():
		var group: int = pending.pop_back()
		var wants_detail := _refine(surface, group, Vector3(24, 10, -20), threshold, false)
		if mask[group] != 0 and not wants_detail:
			selected.append(group)
			requests[group + 1] |= 2
			continue
		var ready := groups[group * 5 + 3] > 0
		for c in groups[group * 5 + 3]:
			var child: int = surface.children[groups[group * 5 + 2] + c]
			ready = ready and _covered(surface, mask, child)
		if ready:
			for c in groups[group * 5 + 3]:
				pending.append(surface.children[groups[group * 5 + 2] + c])
		else:
			selected.append(group)
			requests[group + 1] |= 2
			for c in groups[group * 5 + 3]:
				var child: int = surface.children[groups[group * 5 + 2] + c]
				if not _covered(surface, mask, child):
					requests[child + 1] |= 1
	selected.sort()
	return {"selected": selected, "requests": requests}


func _test_gpu(surface: Dictionary) -> void:
	surface = _forest(surface)
	var failures: Array[String] = []
	var rd := RenderingServer.get_rendering_device()
	var path := ProjectSettings.globalize_path("res://../../servers/rendering/renderer_rd/shaders/forward_clustered/mesh_clod_select.glsl")
	var source := RDShaderSource.new()
	source.source_compute = FileAccess.get_file_as_string(path).replace("#[compute]", "").replace("#VERSION_DEFINES", "#define MODE_RESIDENCY")
	var spirv := rd.shader_compile_spirv_from_source(source)
	if not spirv.compile_error_compute.is_empty():
		_finish.call_deferred([spirv.compile_error_compute])
		return
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)
	var resource := MeshClusterData.new()
	if resource.set_data_checked([surface]) != OK:
		rd.free_rid(pipeline)
		rd.free_rid(shader)
		_finish.call_deferred(["fixture cache invalid"])
		return
	var groups: PackedInt32Array = surface.groups
	var clusters: PackedInt32Array = surface.clusters
	var count := groups.size() / 5
	var group_bytes := PackedByteArray()
	var ranges: Array[Vector2i] = []
	for group in count:
		var position: Vector3 = surface.bounds[group * 2]
		var size: Vector3 = surface.bounds[group * 2 + 1]
		group_bytes.append_array(PackedFloat32Array([position.x, position.y, position.z, surface.errors[group], size.x, size.y, size.z, 0]).to_byte_array())
		var first := groups[group * 5]
		var last := first + groups[group * 5 + 1] - 1
		var offset := clusters[first * 3]
		var length := clusters[last * 3] + clusters[last * 3 + 1] - offset
		ranges.append(Vector2i(offset, length))
		group_bytes.append_array(PackedInt32Array([offset, length, groups[group * 5 + 4], groups[group * 5 + 3]]).to_byte_array())
	var cases: Array[Dictionary] = []
	for threshold in [0.0, 1000000.0]:
		cases.append({"camera": Vector3(24, 10, -20), "threshold": threshold, "ortho": false, "clip": 10000.0})
	var inputs := _encode_inputs(cases, count)
	var commands := PackedInt32Array()
	commands.resize(32 + 2 * count * 5)
	commands.fill(-1)
	var feedback := PackedInt32Array()
	feedback.resize(count + 5)
	for guard in range(count + 1, count + 5):
		feedback[guard] = -1
	var buffers: Array[RID] = [
		rd.storage_buffer_create(group_bytes.size(), group_bytes),
		rd.storage_buffer_create(inputs.size(), inputs),
		rd.storage_buffer_create(commands.size() * 4, commands.to_byte_array(), RenderingDevice.STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT),
		rd.storage_buffer_create((count + 1) * 16),
		rd.storage_buffer_create(surface.children.size() * 4, surface.children.to_byte_array()),
		rd.storage_buffer_create(feedback.size() * 4, feedback.to_byte_array()),
	]
	var uniforms: Array[RDUniform] = []
	for binding in 6:
		var uniform := RDUniform.new()
		uniform.binding = binding
		uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
		uniform.add_id(buffers[binding])
		uniforms.append(uniform)
	var uniform_set := rd.uniform_set_create(uniforms, shader, 0)
	var random := RandomNumberGenerator.new()
	random.seed = 271828
	for step in 24:
		var mask := PackedByteArray()
		mask.resize(count)
		for group in count:
			match step % 6:
				0: mask[group] = 1 # Full residency restores ordinary selection.
				1: mask[group] = int(groups[group * 5 + 4] == -1) # Coarse fallback.
				2: mask[group] = int(groups[group * 5 + 3] == 0) # Evicted ancestors.
				3: mask[group] = 0 # Whole-surface fallback, never a partial cut.
				_: mask[group] = int(random.randf() < 0.4 or groups[group * 5 + 4] == -1)
		if step % 12 == 3:
			mask[0] = 1 # Partially covered surface must still reject the whole cut.
		if step % 12 == 9:
			mask[surface.roots[0]] = 1 # One covered root cannot mask a missing region.
		var state := resource.get_residency_state(0, mask)
		var flags: PackedByteArray = state.groups
		for group in count:
			var expected_flags := int(mask[group]) | (2 if _covered(surface, mask, group) else 0)
			if flags[group] != expected_flags:
				failures.append("native CPU residency coverage differs from recursive oracle")
		var metadata := PackedInt32Array([int(state.complete), 0, 0, 0]).to_byte_array()
		for group in count:
			# z = first index of the resident copy; here the source cache layout itself.
			metadata.append_array(PackedInt32Array([flags[group], groups[group * 5 + 2], ranges[group].x, 0]).to_byte_array())
		rd.buffer_update(buffers[3], 0, metadata.size(), metadata)
		rd.buffer_update(buffers[5], 0, feedback.size() * 4, feedback.to_byte_array())
		var list := rd.compute_list_begin()
		rd.compute_list_bind_compute_pipeline(list, pipeline)
		rd.compute_list_bind_uniform_set(list, uniform_set, 0)
		for draw in 2:
			# Residency push constant: draw index, first request word (0: own buffer).
			rd.compute_list_set_push_constant(list, PackedInt32Array([draw, 0]).to_byte_array(), 8)
			rd.compute_list_dispatch(list, (count + 63) / 64, 1, 1)
		rd.compute_list_end()
		var actual := rd.buffer_get_data(buffers[2]).to_int32_array()
		var expected_feedback := feedback.duplicate()
		for draw in 2:
			var expected := _resident_oracle(surface, mask, cases[draw].threshold)
			var selected: Array[int] = []
			for group in count:
				var offset := 16 + draw * count * 5 + group * 5
				if actual[offset + 1] == 1:
					selected.append(group)
					if mask[group] == 0:
						failures.append("GPU selected a missing representation")
				elif actual[offset + 1] != 0:
					failures.append("GPU instance count outside zero/one")
				if actual[offset] != ranges[group].y or actual[offset + 2] != ranges[group].x or actual[offset + 3] != 0 or actual[offset + 4] != 0:
					failures.append("GPU changed source-index command ranges")
			if selected != expected.selected:
				failures.append("residency cut differs from exclusive-root oracle")
			if state.complete:
				for leaf in count:
					if groups[leaf * 5 + 3] != 0:
						continue
					var hits := 0
					var ancestor := leaf
					while ancestor != -1:
						hits += int(selected.has(ancestor))
						ancestor = groups[ancestor * 5 + 4]
					if hits != 1:
						failures.append("cut has a hole or overlapping ancestors")
			for group in count + 1:
				expected_feedback[group] |= expected.requests[group]
		if rd.buffer_get_data(buffers[5]).to_int32_array() != expected_feedback:
			failures.append("GPU missing/used/fallback feedback or guards differ")
		for guard in 16:
			if actual[guard] != -1 or actual[actual.size() - guard - 1] != -1:
				failures.append("GPU overwrote command guards")
	for rid in [uniform_set, pipeline, shader] + buffers:
		rd.free_rid(rid)
	_finish.call_deferred(failures)


func _finish(failures: Array) -> void:
	if failures.is_empty():
		print("RESIDENCY_TEST_PASS: CPU/GPU complete cuts, missing-page fallback, evicted ancestors, shared-view requests, guards and repeated snapshot changes")
	else:
		for failure in failures:
			printerr("RESIDENCY_TEST_FAIL: ", failure)
	get_tree().quit(0 if failures.is_empty() else 1)
