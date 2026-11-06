extends Node3D
## Two-pass CLOD HZB occlusion (rendering/geometry/cluster_lod/occlusion_two_pass)
## in a real scene, against the same frames rendered without occlusion.
## A: static view behind a wall: hidden groups are left out of the phase-1
##    depth pass and the image is equal to the reference.
## B: the camera alternates every frame between that view and one that sees
##    the spheres: groups hidden in the previous frame are recovered in phase 2,
##    and every frame, including the first after each switch, equals the reference.
## C: with two passes off, nothing is deferred and the image is still equal.
## D: with TAA, and with FSR2 at half resolution (a jittered projection every
##    frame), in a view where the wall edge hides four spheres and cuts one,
##    occlusion runs and the image is equal to the same mode without occlusion.

const SPHERES := 5
var _failures: Array[String] = []
var _camera: Camera3D


func _sphere(segments: int) -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = segments
	sphere.rings = segments / 2
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	if mesh.generate_cluster_lods() != OK:
		_failures.append("cluster LOD generation failed")
	return mesh


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", 0.5)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_two_pass", true)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.1, 0.12, 0.15)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, 30, 0)
	add_child(light)
	# The wall is an ordinary mesh: an occluder only, not CLOD-eligible itself
	# would also work, but CLOD keeps the scene closer to real content.
	var wall := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(6, 3, 0.2)
	wall.mesh = box
	wall.position = Vector3(0, 0, -3)
	add_child(wall)
	for i in SPHERES:
		var node := MeshInstance3D.new()
		node.mesh = _sphere(64 + 16 * i)
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) / SPHERES, 0.7, 0.9)
		node.material_override = material
		node.position = Vector3(-2.0 + i, 0.0, -5.0)
		node.scale = Vector3.ONE * 0.8
		add_child(node)
	_camera = Camera3D.new()
	add_child(_camera)
	_camera.current = true
	_view(0)
	_run.call_deferred()


func _view(index: int) -> void:
	# 0: straight at the wall, every sphere behind it. 1: from the side and above.
	# 2: from the right, the wall edge hides four spheres and cuts the fifth.
	if index == 0:
		_camera.position = Vector3(0, 0, 0)
		_camera.look_at(Vector3(0, 0, -5))
	elif index == 2:
		_camera.position = Vector3(5, 0, 0)
		_camera.look_at(Vector3(0, 0, -5))
	else:
		_camera.position = Vector3(4.5, 2.5, -1.0)
		_camera.look_at(Vector3(0, 0, -5))


func _occlusion(enabled: bool, two_pass: bool) -> void:
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", enabled)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_two_pass", two_pass)


func _frame() -> Image:
	await RenderingServer.frame_post_draw
	return get_viewport().get_texture().get_image()


func _statistics(after_frame: int) -> Dictionary:
	var viewport := get_viewport().get_viewport_rid()
	for attempt in 90:
		await RenderingServer.frame_post_draw
		var result := RenderingServer.viewport_get_gpu_geometry_statistics(viewport)
		if not result.is_empty() and int(result.frame) > after_frame:
			return result
	_failures.append("statistics never arrived")
	return {}


func _reference(index: int) -> Image:
	_occlusion(false, false)
	_view(index)
	for frame in 8:
		await RenderingServer.frame_post_draw
	return await _frame()


func _taa_frame(index: int, enabled: bool, fsr2 := false) -> Image:
	# Turning TAA (or FSR2) off and on drops its history; starting at the same
	# place in the jitter sequence (16 frames for TAA, 8 * 2^2 for FSR2 at half
	# resolution) makes captures repeatable.
	_occlusion(enabled, true)
	_view(index)
	var viewport := get_viewport()
	viewport.use_taa = false
	viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	viewport.scaling_3d_scale = 0.5 if fsr2 else 1.0
	var period := 32 if fsr2 else 16
	await RenderingServer.frame_post_draw
	while Engine.get_frames_drawn() % period != 0:
		await RenderingServer.frame_post_draw
	if fsr2:
		viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
	else:
		viewport.use_taa = true
	for frame in period * 3:
		await RenderingServer.frame_post_draw
	return await _frame()


func _run() -> void:
	var references: Array[Image] = [await _reference(0), await _reference(1)]

	# A. Static occluded view: phase 1 leaves the hidden groups out.
	_occlusion(true, true)
	_view(0)
	for frame in 8:
		await RenderingServer.frame_post_draw
	var a := await _statistics(Engine.get_frames_drawn() + 2)
	if int(a.get("hzb_groups_deferred", 0)) == 0 or int(a.get("hzb_groups_occluded", 0)) < int(a.get("hzb_groups_deferred", 0)) or int(a.get("hzb_groups_recovered", -1)) != 0:
		_failures.append("static: %s deferred, %s occluded, %s recovered groups" % [a.get("hzb_groups_deferred"), a.get("hzb_groups_occluded"), a.get("hzb_groups_recovered")])
	if (await _frame()).get_data() != references[0].get_data():
		_failures.append("static: two-pass image differs from the reference")

	# B. Alternating views: every switch recovers last frame's hidden groups.
	var mismatches := 0
	for frame in 40:
		_view(frame % 2 + 1 if frame % 2 == 0 else 0)
		var image := await _frame()
		if image.get_data() != references[1 if frame % 2 == 0 else 0].get_data():
			mismatches += 1
	if mismatches > 0:
		_failures.append("alternating: %d of 40 frames differ from the reference" % mismatches)
	var counts := {"deferred": 0, "recovered": 0}
	for sample in 6:
		_view(1 if sample % 2 == 0 else 0)
		var b := await _statistics(Engine.get_frames_drawn())
		counts.deferred += int(b.get("hzb_groups_deferred", 0))
		counts.recovered += int(b.get("hzb_groups_recovered", 0))
	if counts.recovered == 0:
		_failures.append("alternating: no group was recovered in phase 2 (%s deferred)" % counts.deferred)

	# C. Two passes off: occlusion still trims color, nothing is deferred.
	_occlusion(true, false)
	_view(0)
	for frame in 8:
		await RenderingServer.frame_post_draw
	var c := await _statistics(Engine.get_frames_drawn() + 2)
	if int(c.get("hzb_groups_deferred", -1)) != 0 or int(c.get("hzb_groups_occluded", 0)) == 0:
		_failures.append("single pass: %s deferred, %s occluded groups" % [c.get("hzb_groups_deferred"), c.get("hzb_groups_occluded")])
	if (await _frame()).get_data() != references[0].get_data():
		_failures.append("single pass: image differs from the reference")

	# D. TAA and FSR2: occlusion is tested with the jittered projection of
	# each frame, at the internal resolution.
	var jittered := {}
	for mode in ["TAA", "FSR2"]:
		var fsr2: bool = mode == "FSR2"
		var reference := await _taa_frame(2, false, fsr2)
		var repeat := await _taa_frame(2, false, fsr2)
		var occluded := await _taa_frame(2, true, fsr2)
		var d := await _statistics(Engine.get_frames_drawn())
		jittered[mode] = int(d.get("hzb_groups_occluded", 0))
		if reference.get_data() != repeat.get_data():
			_failures.append("%s: two captures without occlusion differ, the comparison is not repeatable" % mode)
		elif occluded.get_data() != reference.get_data():
			_failures.append("%s: image with occlusion differs from the reference" % mode)
		if jittered[mode] == 0:
			_failures.append("%s: no group occluded under jitter" % mode)
	get_viewport().use_taa = false
	get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	get_viewport().scaling_3d_scale = 1.0

	if _failures.is_empty():
		print("HZB_TWO_PASS_TEST_PASS: static view defers %d of %d occluded groups from phase-1 depth, 40 alternating frames equal the reference with %d groups recovered, single pass defers none; with TAA %d and FSR2 %d groups occluded, images equal" % [a.get("hzb_groups_deferred"), a.get("hzb_groups_occluded"), counts.recovered, jittered.TAA, jittered.FSR2])
	else:
		for failure in _failures:
			printerr("HZB_TWO_PASS_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
