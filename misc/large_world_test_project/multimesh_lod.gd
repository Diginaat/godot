extends Node3D
## GPU per-instance MultiMesh LOD: exact per-level visible counts against the
## CPU LOD rule, pixel identity with LOD disabled, and coverage against one
## single-instance MultiMesh per instance (CPU LOD then equals per-instance LOD).
## A second MultiMesh covers a LOD range that starts and ends at reduced LODs.

const COUNT := 360
const MID_COUNT := 160
var _failures: Array[String] = []
var _mesh: ArrayMesh
var _sizes: Array[float] = []
var _lod_count := 0
var _edge_scale := 0.0
var _camera: Camera3D
var _gpu_nodes: Array[MultiMeshInstance3D] = []
var _reference: Node3D
var _expected := [0, 0, 0, 0]
var _ranges: Array[Vector2i] = []
var _instances := 0


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	var sphere := SphereMesh.new()
	sphere.radial_segments = 96
	sphere.rings = 48
	var importer := ImporterMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	importer.add_surface(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays(), [], {}, material)
	importer.generate_lods(25.0, 60.0, [])
	_lod_count = importer.get_surface_lod_count(0)
	for lod in _lod_count:
		_sizes.append(importer.get_surface_lod_size(0, lod))
	_mesh = importer.get_mesh()
	if _lod_count < 6:
		_failures.append("expected at least six index LODs, got %d" % _lod_count)
	_camera = Camera3D.new()
	_camera.far = 30000
	_camera.fov = 50
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _cpu_lod(distance: float) -> int:
	# Godot's CPU rule for a node scale of 1: the coarsest LOD whose edge length
	# still projects below the threshold; -1 marks distances too close to a switch.
	var lod := 0
	for k in _lod_count:
		var ratio := _sizes[k] * _edge_scale / distance
		if absf(ratio - 1.0) < 0.08:
			return -1
		if ratio > 1.0:
			break
		lod = k + 1
	return lod


func _bucket_lod(bucket: int, first: int, last: int) -> int:
	# Same mapping as RenderForwardClustered::_multimesh_lod_for_bucket().
	var span := last - first
	return first + (bucket if span < 4 else (bucket * span + 1) / 3)


func _bounds(transforms: Array[Transform3D]) -> AABB:
	var bounds := transforms[0] * _mesh.get_aabb()
	for xform in transforms:
		bounds = bounds.merge(xform * _mesh.get_aabb())
	return bounds


func _levels(transforms: Array[Transform3D]) -> Array:
	# Per-instance GPU level as the renderer picks it, or -1 when an instance
	# lies too close to a threshold for an exact count. Empty when the bounds do.
	var bounds := _bounds(transforms)
	var camera := _camera.global_position
	var nearest := (bounds.position - camera).max(camera - bounds.end).max(Vector3.ZERO)
	var farthest := (camera - bounds.position).abs().max((camera - bounds.end).abs())
	var first := _cpu_lod(nearest.length())
	var last := _cpu_lod(farthest.length())
	var result := []
	if first < 0 or last < 0:
		return result
	var levels := mini(last - first + 1, 4)
	_ranges.append(Vector2i(first, last))
	for xform in transforms:
		var scale := xform.basis.get_scale().x
		var extent := Vector3.ONE * 0.5 * scale
		var distance := ((xform.origin - camera).abs() - extent).max(Vector3.ZERO).length()
		var level := 0
		for k in levels - 1:
			var ratio := _sizes[_bucket_lod(k + 1, first, last) - 1] * _edge_scale * scale / distance
			if absf(ratio - 1.0) < 0.08:
				level = -1
				break
			if ratio > 1.0:
				break
			level = k + 1
		result.append(level)
	return result


func _settle(transforms: Array[Transform3D]) -> Array:
	# Dropping near-threshold instances can move the bounds; repeat until stable.
	for attempt in 8:
		var ranges := _ranges.size()
		var levels := _levels(transforms)
		if levels.is_empty():
			_failures.append("MultiMesh bounds lie on a LOD switch")
			return []
		var kept: Array[Transform3D] = []
		for i in transforms.size():
			if levels[i] >= 0:
				kept.append(transforms[i])
		if kept.size() == transforms.size():
			return levels
		_ranges.resize(ranges)
		transforms.assign(kept)
	_failures.append("fixture placement did not settle")
	return []


func _add_multimesh(transforms: Array[Transform3D], hue: float) -> void:
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = _mesh
	multimesh.instance_count = transforms.size()
	for i in transforms.size():
		multimesh.set_instance_transform(i, transforms[i])
		multimesh.set_instance_color(i, Color.from_hsv(fmod(hue + float(i % 17) / 40.0, 1.0), 0.8, 1.0))
	var node := MultiMeshInstance3D.new()
	node.multimesh = multimesh
	add_child(node)
	_gpu_nodes.append(node)
	_instances += transforms.size()
	# Reference: one single-instance MultiMesh per instance, its transform on the
	# node, so Godot's CPU LOD (per MultiMesh) is a per-instance LOD.
	for i in transforms.size():
		var single := MultiMesh.new()
		single.transform_format = MultiMesh.TRANSFORM_3D
		single.use_colors = true
		single.mesh = _mesh
		single.instance_count = 1
		single.set_instance_transform(0, Transform3D.IDENTITY)
		single.set_instance_color(0, multimesh.get_instance_color(i))
		var reference := MultiMeshInstance3D.new()
		reference.multimesh = single
		reference.transform = transforms[i]
		_reference.add_child(reference)


func _build() -> void:
	var viewport := get_viewport()
	# A 0.1 px threshold moves the LOD switches of this fine sphere to 5-15 m.
	viewport.mesh_lod_threshold = 0.1
	var threshold := viewport.mesh_lod_threshold / float(viewport.get_visible_rect().size.x)
	var multiplier := _camera.get_camera_projection().get_lod_multiplier() / float(ProjectSettings.get_setting("rendering/scaling_3d/scale"))
	_edge_scale = 1.0 / (threshold * multiplier)
	_reference = Node3D.new()
	_reference.visible = false
	add_child(_reference)
	var random := RandomNumberGenerator.new()
	random.seed = 77
	# Set 1 spans LOD 0 to the coarsest LOD. Target each of the four buckets in
	# turn, using the bucket edges of that full range.
	var full: Array[float] = []
	for bucket in range(1, 4):
		full.append(_sizes[_bucket_lod(bucket, 0, _lod_count) - 1] * _edge_scale)
	print("MULTIMESH_LOD_BANDS: edges x scale 1 = %s" % [full])
	var transforms: Array[Transform3D] = []
	var attempts := 0
	while transforms.size() < COUNT and attempts < COUNT * 20:
		attempts += 1
		var scale := random.randf_range(0.6, 2.4)
		var target := attempts % 4
		var low := 0.8 * scale if target == 0 else full[target - 1] * scale * 1.15
		var high := full[target] * scale * 0.87 if target < 3 else full[2] * scale * 2.5
		if high <= low:
			continue
		var distance := random.randf_range(low, high)
		var position := Vector3(random.randf_range(-0.25, 0.25) * distance, random.randf_range(-0.15, 0.15) * distance, -distance)
		transforms.append(Transform3D(Basis.from_scale(Vector3.ONE * scale), position))
	var levels := _settle(transforms)
	var counts := [0, 0, 0, 0]
	for level in levels:
		counts[level] += 1
	for level in 4:
		if counts[level] < 20:
			_failures.append("fixture placed only %d instances at level %d" % [counts[level], level])
		_expected[level] += counts[level]
	if not _ranges.is_empty() and _ranges[0] != Vector2i(0, _lod_count):
		_failures.append("full MultiMesh spans LOD %s, expected 0 to %d" % [_ranges[0], _lod_count])
	_add_multimesh(transforms, 0.0)
	# Set 2 starts beyond the first LOD switch, so level 0 is a reduced LOD and
	# the last level is not the coarsest one.
	var mid: Array[Transform3D] = []
	for i in MID_COUNT:
		var scale := random.randf_range(0.6, 2.4)
		var distance := random.randf_range(full[0] * 2.5, full[1] * 1.5)
		mid.append(Transform3D(Basis.from_scale(Vector3.ONE * scale), Vector3(random.randf_range(0.02, 0.3) * distance, random.randf_range(-0.15, 0.05) * distance, -distance)))
	levels = _settle(mid)
	var mid_range := _ranges[1] if _ranges.size() > 1 else Vector2i(-1, -1)
	if mid_range.x <= 0 or mid_range.y >= _lod_count:
		_failures.append("offset MultiMesh spans LOD %s, not an inner range" % mid_range)
	var used := {}
	for level in levels:
		_expected[level] += 1
		used[level] = true
	if used.size() < 2:
		_failures.append("offset MultiMesh uses only %d GPU levels" % used.size())
	print("MULTIMESH_LOD_RANGE: offset set LOD %s, %d instances, levels used %s" % [mid_range, mid.size(), used.keys()])
	_add_multimesh(mid, 0.5)


func _thinning_hash(index: int) -> float:
	# Same integer hash as thinning_hash() in multimesh_cull.glsl.
	var x := index & 0xffffffff
	x ^= x >> 16
	x = _mul32(x, 0x7feb352d)
	x ^= x >> 15
	x = _mul32(x, 0x846ca68b)
	x ^= x >> 16
	return float(x >> 8) / 16777216.0


func _mul32(a: int, b: int) -> int:
	# Low 32 bits of a 32 x 32-bit product without signed 64-bit overflow.
	var low := (a * (b & 0xffff)) & 0xffffffff
	var high := ((a * (b >> 16)) & 0xffff) << 16
	return (low + high) & 0xffffffff


func _settings(enabled: bool) -> void:
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", enabled)


func _frames(count: int) -> void:
	for frame in count:
		await RenderingServer.frame_post_draw


func _statistics() -> Dictionary:
	var rid := get_viewport().get_viewport_rid()
	var after := Engine.get_frames_drawn() + 3
	for attempt in 60:
		await RenderingServer.frame_post_draw
		var stats := RenderingServer.viewport_get_gpu_geometry_statistics(rid)
		if not stats.is_empty() and int(stats.frame) > after:
			return stats
	_failures.append("statistics never arrived")
	return {}


func _coverage(image: Image) -> PackedByteArray:
	var mask := PackedByteArray()
	mask.resize(image.get_width() * image.get_height())
	for y in image.get_height():
		for x in image.get_width():
			mask[y * image.get_width() + x] = int(image.get_pixel(x, y).get_luminance() > 0.02)
	return mask


func _run() -> void:
	await _frames(2)
	_build()
	# 1. Exact per-level counts from the GPU kernel.
	_settings(true)
	await _frames(10)
	var stats := await _statistics()
	var per_level: PackedInt64Array = stats.get("multimesh_instances_visible_per_lod", PackedInt64Array())
	if per_level.size() != 4:
		_failures.append("missing per-LOD statistics")
	else:
		for level in 4:
			if per_level[level] != _expected[level]:
				_failures.append("per-LOD visible counts GPU %s, expected %s" % [per_level, _expected])
				break
	if int(stats.get("multimesh_instances_visible", -1)) != _instances:
		_failures.append("visible %s of %d in-frustum instances" % [stats.get("multimesh_instances_visible"), _instances])
	# 2. Coverage of the GPU LOD path against per-instance CPU LOD.
	await _frames(10)
	var gpu_image := get_viewport().get_texture().get_image()
	_settings(false)
	for node in _gpu_nodes:
		node.visible = false
	_reference.visible = true
	await _frames(12)
	var reference_image := get_viewport().get_texture().get_image()
	var gpu_mask := _coverage(gpu_image)
	var reference_mask := _coverage(reference_image)
	var covered := 0
	var mismatched := 0
	for i in gpu_mask.size():
		covered += reference_mask[i]
		mismatched += int(gpu_mask[i] != reference_mask[i])
	if covered < 2000 or mismatched > covered / 200:
		_failures.append("coverage: %d of %d reference pixels mismatched" % [mismatched, covered])
	# 3. LOD disabled: the GPU path must be pixel-identical to source rendering.
	_reference.visible = false
	for node in _gpu_nodes:
		node.visible = true
	get_viewport().mesh_lod_threshold = 0.0
	await _frames(12)
	var source := get_viewport().get_texture().get_image()
	_settings(true)
	await _frames(12)
	var culled := get_viewport().get_texture().get_image()
	if source.get_data() != culled.get_data():
		_failures.append("with LOD disabled, GPU culling pixels differ from source")
	# 4. Timing only (no assertion): every instance is in view, so the GPU path
	# differs from source only by its per-instance levels. Source draws each
	# MultiMesh at the LOD of its nearest point.
	get_viewport().mesh_lod_threshold = 0.1
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var timings := { true: [], false: [] }
	for pass_index in 4:
		var enabled := pass_index % 2 == 0
		_settings(enabled)
		await _frames(10)
		for frame in 60:
			await RenderingServer.frame_post_draw
			timings[enabled].append(RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()))
	for enabled in timings:
		timings[enabled].sort()
	print("MULTIMESH_LOD_TIMING: gpu_p50 source=%.3f gpu_lod=%.3f ms" % [timings[false][60], timings[true][60]])
	# 5. Distance thinning on the first MultiMesh: visible count against the
	# kernel's hash rule (instance-centre distance; ±1 for float rounding).
	_settings(true)
	var thinned: MultiMesh = _gpu_nodes[0].multimesh
	thinned.distance_thinning_begin = 50.0
	thinned.distance_thinning_end = 3000.0
	var expected_visible := _gpu_nodes[1].multimesh.instance_count
	for i in thinned.instance_count:
		var distance := thinned.get_instance_transform(i).origin.distance_to(_camera.global_position)
		var kept := clampf((3000.0 - distance) / (3000.0 - 50.0), 0.0, 1.0)
		expected_visible += int(_thinning_hash(i) < kept)
	await _frames(6)
	var thin_stats := await _statistics()
	var thin_visible := int(thin_stats.get("multimesh_instances_visible", -1))
	if absi(thin_visible - expected_visible) > 1 or expected_visible >= _instances:
		_failures.append("thinning: %d visible, expected %d of %d" % [thin_visible, expected_visible, _instances])
	thinned.distance_thinning_end = 0.0
	print("MULTIMESH_LOD_THINNING: %d of %d instances kept (expected %d)" % [thin_visible, _instances, expected_visible])
	var output := ProjectSettings.globalize_path("res://../../devtools/out/large_world")
	var driver := RenderingServer.get_current_rendering_driver_name()
	gpu_image.save_png(output.path_join("multimesh_lod_%s_gpu.png" % driver))
	reference_image.save_png(output.path_join("multimesh_lod_%s_reference.png" % driver))
	if _failures.is_empty():
		print("MULTIMESH_LOD_TEST_PASS: %d instances, %d LODs, levels %s exact; coverage mismatch %d of %d px; LOD-off pixels identical" % [_instances, _lod_count, _expected, mismatched, covered])
	else:
		for failure in _failures:
			printerr("MULTIMESH_LOD_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
