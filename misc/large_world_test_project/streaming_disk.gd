extends Node3D
## Cluster LOD streaming from page files (MeshClusterData.save_pages), against
## identical fully resident copies.
## A: pages read from disk converge to the resident image byte for byte, with
##    only the roots held in system memory.
## B: a pool rebuild drops every page; they come back from disk.
## C: a page file with out-of-range indices fails its reads once per group; the
##    surface stays complete (ancestors draw) and nothing is read again.
## D: a page file that does not match its mesh is rejected; the mesh streams
##    from memory and still matches the resident image.
## E: meshes freed while reads are in flight release cleanly.
## In the close view only mesh 0 needs groups beyond its roots, so the damaged
## and stale file cases use mesh 0.

const MESHES := 3
var _failures: Array[String] = []
var _camera: Camera3D
var _streamed: Node3D
var _resident: Node3D
var _paths: Array[String] = []


func _sphere(segments: int) -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = segments
	sphere.rings = segments / 2
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	if mesh.generate_cluster_lods() != OK:
		_failures.append("cluster LOD generation failed")
	return mesh


func _set_of_meshes(paged: bool) -> Node3D:
	var root := Node3D.new()
	add_child(root)
	for i in MESHES:
		var node := MeshInstance3D.new()
		var mesh := _sphere(96 + 32 * i)
		if paged:
			var path := "user://streaming_disk_%d.clodpages" % i
			_paths.append(path)
			if mesh.cluster_data.save_pages(path) != OK or mesh.cluster_data.get_page_file() != path:
				_failures.append("save_pages failed for mesh %d" % i)
		node.mesh = mesh
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) / MESHES, 0.7, 0.9)
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
	_close_view()
	_run.call_deferred()


func _close_view() -> void:
	# Close enough that every sphere needs many groups beyond its roots.
	_camera.position = Vector3(0.0, 0.4, -1.5)
	_camera.look_at(Vector3(0.0, 0.0, -7.0))


func _frames(count: int) -> void:
	for frame in count:
		await RenderingServer.frame_post_draw


func _stats() -> Dictionary:
	return RenderingServer.viewport_get_gpu_geometry_statistics(get_viewport().get_viewport_rid())


func _settle() -> Dictionary:
	# Until loads and disk reads stop changing for 30 frames, with nothing in flight.
	var last := ""
	var stable := 0
	var stats := {}
	for frame in 900:
		await RenderingServer.frame_post_draw
		stats = _stats()
		var key := "%s/%s/%s" % [stats.get("clod_stream_loads", -1), stats.get("clod_stream_disk_reads", -1), stats.get("clod_stream_disk_errors", -1)]
		stable = stable + 1 if key == last and int(stats.get("clod_stream_disk_reading_bytes", 1)) == 0 else 0
		last = key
		if stable >= 30:
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


func _compare(label: String) -> void:
	var streamed := await _show(true)
	var resident := await _show(false)
	_streamed.visible = true
	_resident.visible = false
	if streamed.get_data() != resident.get_data():
		var m := _mismatch(streamed, resident)
		_failures.append("%s: streamed image differs from resident (%d of %d coverage px)" % [label, m.x, m.y])


func _reset_mesh(index: int) -> void:
	# New cluster data reaches the renderer as a new setup (new page file check),
	# which reads the streaming setting again. Resident meshes are not set up again.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", true)
	var mesh: ArrayMesh = (_streamed.get_child(index) as MeshInstance3D).mesh
	mesh.cluster_data.emit_changed()


func _run() -> void:
	# Mesh creation reads the streaming setting on the render thread; let each
	# set's cluster data arrive before switching the setting.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", true)
	_streamed = _set_of_meshes(true)
	await _frames(4)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", false)
	_resident = _set_of_meshes(false)
	_resident.visible = false
	await _frames(4)

	# A. Pages from disk; only the roots stay in system memory.
	var a := await _settle()
	var cooked := int(a.get("clod_stream_cooked_bytes", 0))
	var held := int(a.get("clod_stream_memory_bytes", -1))
	if int(a.get("clod_stream_disk_surfaces", 0)) != MESHES or int(a.get("clod_stream_complete_surfaces", 0)) != MESHES:
		_failures.append("disk: %s of %d surfaces paged from disk/complete" % [[a.get("clod_stream_disk_surfaces"), a.get("clod_stream_complete_surfaces")], MESHES])
	if int(a.get("clod_stream_disk_reads", 0)) < 5 * MESHES or int(a.get("clod_stream_disk_errors", -1)) != 0:
		_failures.append("disk: %s reads, %s errors" % [a.get("clod_stream_disk_reads"), a.get("clod_stream_disk_errors")])
	if held <= 0 or held * 4 > cooked:
		_failures.append("disk: %d of %d cooked bytes held in memory (expected only the roots)" % [held, cooked])
	await _compare("disk")

	# B. A budget change rebuilds the pool with only the roots; pages return from disk.
	var reads_before := int(a.get("clod_stream_disk_reads", 0))
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", 64.01)
	var b := await _settle()
	var reread := int(b.get("clod_stream_disk_reads", 0)) - reads_before
	if reread < 5 * MESHES:
		_failures.append("rebuild: no page was read again (%d reads)" % reread)
	await _compare("rebuild")

	# C. Out-of-range indices in mesh 0's pages: reads fail once per group, the
	# surface stays complete with coarser groups, and no read repeats.
	var corrupt := FileAccess.open(_paths[0], FileAccess.READ_WRITE)
	var length := corrupt.get_length()
	corrupt.seek(12 + 48)
	var junk := PackedByteArray()
	junk.resize(length - 60)
	junk.fill(0xff)
	corrupt.store_buffer(junk)
	corrupt.close()
	_reset_mesh(0)
	var c := await _settle()
	var errors := int(c.get("clod_stream_disk_errors", 0))
	if errors == 0 or int(c.get("clod_stream_complete_surfaces", 0)) != MESHES or int(c.get("clod_stream_disk_surfaces", 0)) != MESHES:
		_failures.append("corrupt: %d read errors, %s complete, %s from disk" % [errors, c.get("clod_stream_complete_surfaces"), c.get("clod_stream_disk_surfaces")])
	_camera.position = Vector3(0.0, 0.2, 1.5)
	await _frames(30)
	_close_view()
	var c2 := await _settle()
	if int(c2.get("clod_stream_disk_errors", 0)) != errors:
		_failures.append("corrupt: failed groups were read again (%d then %s errors)" % [errors, c2.get("clod_stream_disk_errors")])
	var streamed_c := await _show(true)
	var resident_c := await _show(false)
	_streamed.visible = true
	_resident.visible = false
	var m := _mismatch(streamed_c, resident_c)
	if m.y < 1000 or m.x * 20 > m.y:
		_failures.append("corrupt: coverage differs in %d of %d px" % [m.x, m.y])

	# D. Mesh 0 now points at a file with another mesh's hash: rejected (with a
	# warning), streamed from memory, image identical again.
	var other := FileAccess.get_file_as_bytes(_paths[1])
	var stale := FileAccess.open(_paths[0], FileAccess.WRITE)
	stale.store_buffer(other)
	stale.close()
	_reset_mesh(0)
	var d := await _settle()
	if int(d.get("clod_stream_disk_surfaces", 0)) != MESHES - 1 or int(d.get("clod_stream_complete_surfaces", 0)) != MESHES:
		_failures.append("stale: %s surfaces from disk, %s complete" % [d.get("clod_stream_disk_surfaces"), d.get("clod_stream_complete_surfaces")])
	await _compare("stale")

	# E. Mesh 0 (the only one close enough to need more than its roots here) gets
	# a good page file again; free the meshes while its first reads are in flight.
	var mesh_0: ArrayMesh = (_streamed.get_child(0) as MeshInstance3D).mesh
	if mesh_0.cluster_data.save_pages(_paths[0]) != OK:
		_failures.append("free: save_pages failed")
	var in_flight := 0
	for frame in 60:
		await RenderingServer.frame_post_draw
		in_flight = int(_stats().get("clod_stream_disk_reading_bytes", 0))
		if in_flight > 0:
			break
	if in_flight == 0:
		_failures.append("free: no read was in flight")
	_streamed.queue_free()
	_streamed = null
	mesh_0 = null
	await _frames(30)
	var e := _stats()
	if int(e.get("clod_stream_surfaces", -1)) != 0 or int(e.get("clod_stream_disk_reading_bytes", -1)) != 0:
		_failures.append("free: %s surfaces, %s bytes still reading" % [e.get("clod_stream_surfaces"), e.get("clod_stream_disk_reading_bytes")])

	for path in _paths:
		DirAccess.remove_absolute(path)
	if _failures.is_empty():
		print("STREAMING_DISK_TEST_PASS: %d meshes, %d of %d cooked bytes held; %d pages read from disk, identical to resident; %d read again after a pool rebuild; %d damaged pages rejected once, coverage kept; mismatched file streams from memory; freed with %d bytes in flight" % [MESHES, held, cooked, int(a.get("clod_stream_disk_reads", 0)), reread, errors, in_flight])
	else:
		for failure in _failures:
			printerr("STREAMING_DISK_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
