extends Node3D
## Cluster LOD page files written by scene import (meshes/cluster_lod_page_files).
## A: the imported mesh records a page file in the imported folder, the file
##    starts with the page magic and the .import lists it as a generated file
##    (so it is exported).
##    The saved mesh holds only its roots' indices (indices paged out), and
##    a paged-out record that was tampered with is rejected.
## B: with streaming on, the imported mesh pages from that file with only its
##    roots in system memory, and draws the same image as a resident copy that
##    read the whole file at setup.
## C: a paged-out mesh whose page file is missing draws without cluster LOD.

const PAGE_MAGIC := 0x504c4347
var _failures: Array[String] = []
var _streamed: Node3D
var _resident: Node3D
var _missing: MeshInstance3D
var _summary := ""


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


func _check_file(mesh: ArrayMesh) -> void:
	var path := mesh.cluster_data.get_page_file()
	if not path.begins_with("res://.godot/imported/paged.gltf-") or not path.ends_with(".clodpages"):
		_failures.append("unexpected page file path: \"%s\"" % path)
		return
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_32() != PAGE_MAGIC:
		_failures.append("page file missing or without page magic: " + path)
	var config := ConfigFile.new()
	if config.load("res://import_fixtures/paged.gltf.import") != OK:
		_failures.append("cannot read paged.gltf.import")
	elif not path in config.get_value("deps", "files", []) or not path in config.get_value("deps", "dest_files", []):
		_failures.append(".import does not list the page file as generated/destination file")
	var data := mesh.cluster_data
	var surface := data.get_surface_data(0)
	var held := (surface["indices"] as PackedInt32Array).size()
	if not data.are_indices_paged() or not surface.has("index_count") or held <= 0 or held * 4 > int(surface["index_count"]):
		_failures.append("imported mesh did not page out its indices (%d held of %s)" % [held, surface.get("index_count")])
	var tampered := data.get_data()
	(tampered[0] as Dictionary)["indices"] = (surface["indices"] as PackedInt32Array).slice(3)
	if MeshClusterData.new().set_data_checked(tampered) != ERR_INVALID_DATA:
		_failures.append("paged-out record with a wrong root index count was accepted")
	tampered = data.get_data()
	(tampered[0] as Dictionary).erase("page_file")
	(tampered[0] as Dictionary).erase("page_hash")
	(tampered[0] as Dictionary).erase("page_surface")
	if MeshClusterData.new().set_data_checked(tampered) != ERR_INVALID_DATA:
		_failures.append("paged-out record without a page file was accepted")
	var copy := MeshClusterData.new()
	if copy.set_data_checked(data.get_data()) != OK or not copy.are_indices_paged():
		_failures.append("paged-out record did not round-trip")


func _run() -> void:
	# Mesh creation reads the streaming setting on the render thread.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", true)
	var scene := load("res://import_fixtures/paged.gltf") as PackedScene
	if scene == null:
		_failures.append("paged scene import failed")
		_finish()
		return
	_streamed = scene.instantiate()
	add_child(_streamed)
	var meshes := _streamed.find_children("*", "MeshInstance3D", true, false)
	var mesh: ArrayMesh = meshes[0].mesh if meshes.size() == 1 else null
	if mesh == null or mesh.cluster_data == null or not mesh.cluster_data.matches_mesh(mesh):
		_failures.append("paged import has no single mesh with matching cluster data")
		_finish()
		return
	_check_file(mesh)
	await _frames(4)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", false)
	_resident = MeshInstance3D.new()
	_resident.mesh = mesh.duplicate(true)
	_resident.visible = false
	add_child(_resident)
	await _frames(4)

	var stats := await _settle()
	var cooked := int(stats.get("clod_stream_cooked_bytes", 0))
	var held := int(stats.get("clod_stream_memory_bytes", -1))
	if int(stats.get("clod_stream_disk_surfaces", 0)) != 1 or int(stats.get("clod_stream_complete_surfaces", 0)) != 1:
		_failures.append("%s disk / %s complete streamed surfaces (expected 1)" % [stats.get("clod_stream_disk_surfaces"), stats.get("clod_stream_complete_surfaces")])
	if int(stats.get("clod_stream_disk_reads", 0)) < 2 or int(stats.get("clod_stream_disk_errors", -1)) != 0:
		_failures.append("%s disk reads, %s errors" % [stats.get("clod_stream_disk_reads"), stats.get("clod_stream_disk_errors")])
	if held <= 0 or held * 4 > cooked:
		_failures.append("%d of %d cooked bytes held in memory (expected only the roots)" % [held, cooked])
	_summary = "%s disk reads, %d of %d index bytes held" % [stats.get("clod_stream_disk_reads"), held, cooked]
	var streamed_image := await _show(true)
	var resident_image := await _show(false)
	if streamed_image.get_data() != resident_image.get_data():
		_failures.append("streamed image differs from resident")

	# C. Missing page file: with streaming on, then off, nothing streams or crashes.
	var missing_data := mesh.cluster_data.get_data()
	(missing_data[0] as Dictionary)["page_file"] = "user://import_paged_missing.clodpages"
	for streaming in [true, false]:
		ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", streaming)
		var missing_mesh: ArrayMesh = mesh.duplicate(false)
		var missing_clusters := MeshClusterData.new()
		if missing_clusters.set_data_checked(missing_data) != OK:
			_failures.append("missing-file record rejected by validation")
			break
		missing_mesh.cluster_data = missing_clusters
		_missing = MeshInstance3D.new()
		_missing.mesh = missing_mesh
		_streamed.visible = false
		add_child(_missing)
		await _frames(8)
		var after := _stats()
		if int(after.get("clod_stream_surfaces", 0)) != 1:
			_failures.append("missing page file (streaming %s): %s streamed surfaces (expected only the first mesh)" % [streaming, after.get("clod_stream_surfaces")])
		var image := get_viewport().get_texture().get_image()
		if _mismatch(image, resident_image) > image.get_width() * image.get_height() / 50:
			_failures.append("missing page file (streaming %s): conventional image does not cover the mesh" % streaming)
		_missing.queue_free()
		await _frames(2)
	_finish()


func _mismatch(a: Image, b: Image) -> int:
	var differing := 0
	for y in a.get_height():
		for x in a.get_width():
			differing += int((a.get_pixel(x, y).get_luminance() > 0.02) != (b.get_pixel(x, y).get_luminance() > 0.02))
	return differing


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", false)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", 0.5)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/budget_mb", 64.0)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, 30, 0)
	add_child(light)
	var camera := Camera3D.new()
	add_child(camera)
	camera.current = true
	# Close to one corner, so near groups refine and far groups stay coarse.
	camera.position = Vector3(-1.6, 0.5, 1.6)
	camera.look_at(Vector3(0.5, 0.0, -0.5))
	_run.call_deferred()


func _finish() -> void:
	if _failures.is_empty():
		print("IMPORT_PAGED_TEST_PASS: imported page file recorded and listed for export, indices paged out and checked, streamed from disk equal to a resident copy read from the file, missing file falls back (%s)" % _summary)
	else:
		for failure in _failures:
			printerr("IMPORT_PAGED_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
