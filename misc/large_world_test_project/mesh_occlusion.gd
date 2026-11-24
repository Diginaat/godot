extends Node3D
## HZB occlusion of CPU render list meshes (rendering/geometry/mesh_occlusion/*).
## Spheres with a visibility range stay on the CPU list (the GPU path does not
## take them): 20 behind a wall, 5 in front of it, 3 over its top edge.
## A: one pass: the 20 hidden spheres are left out of the colour pass, the
##    image equals the reference (occlusion off).
## B: two passes (the default): the 20 are also left out of the first depth
##    pass; static views and 30 frames alternating between the front and back
##    views (each frame gets the other view's history, so phase 2 recovers
##    what became visible) equal the reference exactly.
## C: with the settings off, nothing is occluded or deferred.

const ENABLED := "rendering/geometry/mesh_occlusion/enabled"
const TWO_PASS := "rendering/geometry/mesh_occlusion/two_pass"
const HIDDEN := 20

var _failures: Array[String] = []
var _camera: Camera3D


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	# Adaptive VRS (the default) picks each frame's shading rates from the
	# previous frame, so captures compared pixel for pixel would differ.
	get_viewport().vrs_mode = Viewport.VRS_DISABLED
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", false)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.1, 0.12, 0.15)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	# SSAO reads the depth pre-pass: a mesh missing from the depth passes
	# changes the image. (The colour pass writes depth itself, so without a
	# depth reader a missing phase-2 draw would not show.)
	environment.ssao_enabled = true
	environment.ssao_intensity = 4.0
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, 30, 0)
	add_child(light)
	var wall := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(10, 6, 0.5)
	wall.mesh = box
	wall.position = Vector3(0, 1, 0)
	add_child(wall)
	for i in 28:
		var sphere := SphereMesh.new()
		sphere.radius = 0.3
		sphere.height = 0.6
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) / 28.0, 0.7, 0.9)
		sphere.material = material
		var node := MeshInstance3D.new()
		node.mesh = sphere
		node.visibility_range_end = 1000.0
		if i < HIDDEN:
			node.position = Vector3(-4.0 + (i % 5) * 2.0, -1.0 + (i / 5) * 1.0, -3.0)
		elif i < 25:
			node.position = Vector3(-4.0 + (i - 20) * 2.0, 0.0, 2.0)
		else:
			node.position = Vector3(-2.0 + (i - 25) * 2.0, 4.25, -0.6)
		add_child(node)
	_camera = Camera3D.new()
	add_child(_camera)
	_camera.current = true
	_view(0)
	_run.call_deferred()


func _view(index: int) -> void:
	# 0: in front of the wall. 1: behind it, looking back.
	if index == 0:
		_camera.position = Vector3(0, 1, 8)
		_camera.rotation_degrees = Vector3.ZERO
	else:
		_camera.position = Vector3(0, 1, -8)
		_camera.rotation_degrees = Vector3(0, 180, 0)


func _settings(enabled: bool, two_pass: bool) -> void:
	ProjectSettings.set_setting(ENABLED, enabled)
	ProjectSettings.set_setting(TWO_PASS, two_pass)


func _frame() -> Image:
	await RenderingServer.frame_post_draw
	return get_viewport().get_texture().get_image()


func _settle(frames := 8) -> void:
	for frame in frames:
		await RenderingServer.frame_post_draw


func _statistics() -> Dictionary:
	var viewport := get_viewport().get_viewport_rid()
	var after := Engine.get_frames_drawn()
	for attempt in 90:
		await RenderingServer.frame_post_draw
		var result := RenderingServer.viewport_get_gpu_geometry_statistics(viewport)
		if not result.is_empty() and int(result.frame) > after + 1:
			return result
	_failures.append("statistics never arrived")
	return {}


func _check_view(label: String, index: int, reference: Image, occluded: int, deferred: int) -> void:
	_view(index)
	await _settle()
	if (await _frame()).get_data() != reference.get_data():
		_failures.append("%s view %d: image differs from the reference" % [label, index])
	var stats := await _statistics()
	if int(stats.get("meshes_occluded", -1)) != occluded:
		_failures.append("%s view %d: %d meshes occluded, expected %d" % [label, index, int(stats.get("meshes_occluded", -1)), occluded])
	if int(stats.get("meshes_depth_deferred", -1)) != deferred:
		_failures.append("%s view %d: %d meshes deferred from phase-1 depth, expected %d" % [label, index, int(stats.get("meshes_depth_deferred", -1)), deferred])


func _run() -> void:
	_settings(false, false)
	var references: Array[Image] = []
	for index in 2:
		_view(index)
		await _settle()
		references.append(await _frame())
	if references[0].get_data() == references[1].get_data():
		_failures.append("the two views render the same image")

	# A. One pass.
	_settings(true, false)
	await _check_view("one-pass", 0, references[0], HIDDEN, 0)

	# B. Two passes. From behind, the 5 front spheres are hidden.
	_settings(true, true)
	await _check_view("two-pass", 0, references[0], HIDDEN, HIDDEN)
	await _check_view("two-pass", 1, references[1], 5, 5)
	var mismatches := 0
	for frame in 30:
		var index := frame % 2
		_view(index)
		if (await _frame()).get_data() != references[index].get_data():
			mismatches += 1
	if mismatches > 0:
		_failures.append("alternating: %d of 30 frames differ from the reference" % mismatches)

	# C. Off.
	_settings(false, true)
	await _check_view("off", 0, references[0], 0, 0)

	_settings(true, true)
	if _failures.is_empty():
		print("MESH_OCCLUSION_TEST_PASS: %d hidden meshes left out of the colour pass and the phase-1 depth pass, images equal the reference in static and 30 alternating frames, one-pass and off match" % HIDDEN)
	else:
		for failure in _failures:
			print("MESH_OCCLUSION_TEST_FAIL: " + failure)
	get_tree().quit(1 if not _failures.is_empty() else 0)
