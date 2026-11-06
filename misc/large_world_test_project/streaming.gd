extends Node3D
## Cluster LOD streaming: streamed meshes against identical fully resident
## copies. A: ample budget converges to the resident image byte for byte.
## B: two close views whose union does not fit the pool alternate; coverage must
## match (no holes), the pool budget holds, and pressure shows as
## evictions/deferrals.
## C: a pool too small for the roots falls back to source rendering exactly.

const MESHES := 3
var _failures: Array[String] = []
var _camera: Camera3D
var _streamed: Node3D
var _resident: Node3D


func _sphere(segments: int) -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = segments
	sphere.rings = segments / 2
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	if mesh.generate_cluster_lods() != OK:
		_failures.append("cluster LOD generation failed")
	return mesh


func _set_of_meshes(color_shift: float) -> Node3D:
	var root := Node3D.new()
	add_child(root)
	for i in MESHES:
		var node := MeshInstance3D.new()
		node.mesh = _sphere(96 + 32 * i)
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) / MESHES + color_shift, 0.7, 0.9)
		node.material_override = material
		node.position = Vector3(-2.2 + 2.2 * i, 0, -5.0 - 2.0 * i)
		root.add_child(node)
	return root


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", false)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", 0.5)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", 64.0)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, 30, 0)
	add_child(light)
	_camera = Camera3D.new()
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _frames(count: int) -> void:
	for frame in count:
		await RenderingServer.frame_post_draw


func _stats() -> Dictionary:
	return RenderingServer.viewport_get_gpu_geometry_statistics(get_viewport().get_viewport_rid())


func _settle() -> Dictionary:
	# Until loads stop changing for 30 frames (feedback arrives every few frames).
	var last := -1
	var stable := 0
	var stats := {}
	for frame in 600:
		await RenderingServer.frame_post_draw
		stats = _stats()
		var loads := int(stats.get("clod_stream_loads", -1))
		stable = stable + 1 if loads == last else 0
		last = loads
		if stable >= 30 and loads >= 0:
			return stats
	_failures.append("streaming did not settle")
	return stats


func _show(streamed: bool) -> Image:
	_streamed.visible = streamed
	_resident.visible = not streamed
	await _frames(8)
	return get_viewport().get_texture().get_image()


func _mismatch(a: Image, b: Image) -> Vector2i:
	var covered := 0
	var differing := 0
	for y in a.get_height():
		for x in a.get_width():
			var pa := a.get_pixel(x, y).get_luminance() > 0.02
			var pb := b.get_pixel(x, y).get_luminance() > 0.02
			covered += int(pb)
			differing += int(pa != pb)
	return Vector2i(differing, covered)


func _holes(streamed: Image, resident: Image) -> int:
	# Uncovered streamed pixels at least 6 px inside the resident coverage. A
	# coarser cut only moves silhouettes; a missing group leaves interior holes.
	var w := resident.get_width()
	var h := resident.get_height()
	var covered := PackedByteArray()
	covered.resize(w * h)
	for y in h:
		for x in w:
			covered[y * w + x] = int(resident.get_pixel(x, y).get_luminance() > 0.02)
	var holes := 0
	for y in range(6, h - 6, 2):
		for x in range(6, w - 6, 2):
			if streamed.get_pixel(x, y).get_luminance() > 0.02:
				continue
			var interior := true
			for dy in [-6, 0, 6]:
				for dx in [-6, 0, 6]:
					interior = interior and covered[(y + dy) * w + x + dx] == 1
			holes += int(interior)
	return holes


func _pressure_view(view: int) -> void:
	# View 0: close to the two near spheres. View 1: close to the far, densest one.
	_camera.position = Vector3(-1.1, 0.3, -2.6) if view == 0 else Vector3(1.6, 0.3, -6.4)
	_camera.look_at(Vector3(-1.1, 0, -6.0) if view == 0 else Vector3(2.2, 0, -9.0))


func _run() -> void:
	# Mesh creation reads the streaming setting on the render thread; let each
	# set's cluster data arrive before switching the setting.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", true)
	_streamed = _set_of_meshes(0.0)
	await _frames(4)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", false)
	_resident = _set_of_meshes(0.0)
	_resident.visible = false
	await _frames(4)

	# A. Ample budget: after convergence the streamed cut equals the resident one.
	var ample := await _settle()
	if int(ample.get("clod_stream_surfaces", 0)) != MESHES or int(ample.get("clod_stream_complete_surfaces", 0)) != MESHES:
		_failures.append("ample: %s of %d surfaces streamed/complete" % [[ample.get("clod_stream_surfaces"), ample.get("clod_stream_complete_surfaces")], MESHES])
	if int(ample.get("clod_stream_loads", 0)) <= MESHES:
		_failures.append("ample: no group beyond the roots was streamed in")
	var streamed_a := await _show(true)
	var resident_a := await _show(false)
	if streamed_a.get_data() != resident_a.get_data():
		var m := _mismatch(streamed_a, resident_a)
		_failures.append("ample: streamed image differs from resident (%d of %d coverage px)" % [m.x, m.y])
	var cooked := int(ample.get("clod_stream_cooked_bytes", 0))
	# The Performance monitor reads the same root-viewport counter.
	var monitored := int(Performance.get_monitor(Performance.RENDER_CLOD_STREAMING_MEM_USED))
	if monitored != int(_stats().get("clod_stream_resident_bytes", -1)) or monitored <= 0:
		_failures.append("monitor: streaming memory %d B, statistics %s B" % [monitored, _stats().get("clod_stream_resident_bytes")])

	# B. Pressure. Measure what each close view needs alone (a budget change
	# rebuilds the pool with only the roots) and what both need together, then
	# pick a pool between the larger single view and the union: each view fits,
	# alternating them must evict or defer.
	_streamed.visible = true
	_resident.visible = false
	var needs: Array[int] = []
	for view in 2:
		ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", 64.0 + 0.01 * (view + 1))
		_pressure_view(view)
		needs.append(int((await _settle()).get("clod_stream_resident_bytes", 0)))
	_pressure_view(0)
	var union := int((await _settle()).get("clod_stream_resident_bytes", 0))
	var single := maxi(needs[0], needs[1])
	if union < single * 1.1:
		_failures.append("pressure: views share their groups (alone %s, together %d B)" % [needs, union])
	var budget_mb := (single + union) * 0.5 / 1048576.0
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", budget_mb)
	var worst := Vector2i.ZERO
	var pressure := {}
	for view in 4:
		_pressure_view(view % 2)
		pressure = await _settle()
		var streamed_b := await _show(true)
		var resident_b := await _show(false)
		_streamed.visible = true
		_resident.visible = false
		var m := _mismatch(streamed_b, resident_b)
		var holes := _holes(streamed_b, resident_b)
		if m.y < 1000 or holes > 0:
			_failures.append("pressure view %d: %d interior holes (%d of %d coverage px differ)" % [view, holes, m.x, m.y])
		if m.x >= worst.x:
			worst = m
		if int(pressure.get("clod_stream_resident_bytes", 0)) > int(pressure.get("clod_stream_pool_bytes", 0)):
			_failures.append("pressure: resident bytes exceed the pool")
		if int(pressure.get("clod_stream_complete_surfaces", 0)) != MESHES:
			_failures.append("pressure: roots did not stay resident")
	if int(pressure.get("clod_stream_evictions", 0)) + int(pressure.get("clod_stream_deferred", 0)) == 0:
		_failures.append("pressure: the budget never constrained streaming")

	# C. Roots do not fit: streamed surfaces fall back to source rendering.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", 0.0001)
	await _frames(10)
	var tiny := _stats()
	if int(tiny.get("clod_stream_complete_surfaces", -1)) != 0:
		_failures.append("tiny pool: %s surfaces still complete" % tiny.get("clod_stream_complete_surfaces"))
	var fallback := await _show(true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", false)
	var source := await _show(true)
	if fallback.get_data() != source.get_data():
		var m := _mismatch(fallback, source)
		_failures.append("tiny pool: fallback differs from source rendering (%d of %d coverage px)" % [m.x, m.y])
	var output := ProjectSettings.globalize_path("res://../../devtools/out/large_world")
	var driver := RenderingServer.get_current_rendering_driver_name()
	streamed_a.save_png(output.path_join("streaming_%s_ample.png" % driver))
	if _failures.is_empty():
		print("STREAMING_TEST_PASS: %d meshes, cooked %d B; ample pool converges to the resident image; pool of %d B between one view (%d B) and both (%d B): %d loads, %d evictions, %d deferred, worst silhouette difference %d of %d px, no holes; tiny pool falls back to source" % [MESHES, cooked, int(budget_mb * 1048576.0), maxi(needs[0], needs[1]), union, pressure.get("clod_stream_loads", 0), pressure.get("clod_stream_evictions", 0), pressure.get("clod_stream_deferred", 0), worst.x, worst.y])
	else:
		for failure in _failures:
			printerr("STREAMING_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
