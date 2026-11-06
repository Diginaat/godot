extends Node3D
## MultiMesh HZB occlusion (rendering/geometry/multimesh_gpu_culling/occlusion)
## against the same frames rendered without it.
## A: coloured LOD spheres in rows behind and beside a wall, on a rotated and
##    scaled node, at distances that span several GPU LOD levels. Two views
##    and 30 frames alternating between them equal the reference exactly.
## B: a MultiMesh fully inside the frustum and fully behind the wall: it is
##    still culled on the GPU, and all its instances are occluded.
## C: with TAA (a jittered projection), the image equals TAA without occlusion.
## D: with the setting off, nothing is occluded.

var _failures: Array[String] = []
var _camera: Camera3D
var _rows: MultiMeshInstance3D
var _hidden: MultiMeshInstance3D
var _hidden_count := 0


func _lod_sphere() -> Mesh:
	var sphere := SphereMesh.new()
	sphere.radius = 0.3
	sphere.height = 0.6
	sphere.radial_segments = 64
	sphere.rings = 32
	var importer := ImporterMesh.new()
	var material := StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	importer.add_surface(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays(), [], {}, material)
	importer.generate_lods(25.0, 60.0, [])
	if importer.get_surface_lod_count(0) < 3:
		_failures.append("expected index LODs, got %d" % importer.get_surface_lod_count(0))
	return importer.get_mesh()


func _multimesh(mesh: Mesh, transforms: Array[Transform3D]) -> MultiMeshInstance3D:
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = mesh
	multimesh.instance_count = transforms.size()
	for i in transforms.size():
		multimesh.set_instance_transform(i, transforms[i])
		multimesh.set_instance_color(i, Color.from_hsv(fmod(float(i) * 0.137, 1.0), 0.8, 1.0))
	var node := MultiMeshInstance3D.new()
	node.multimesh = multimesh
	add_child(node)
	return node


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", false)
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
	var wall := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(8, 4, 0.3)
	wall.mesh = box
	wall.position = Vector3(0, 0, -6)
	add_child(wall)
	var mesh := _lod_sphere()
	var rows: Array[Transform3D] = []
	for z in 9:
		for y in 3:
			for x in 25:
				var basis := Basis(Vector3.UP, float(x) * 0.3).scaled(Vector3.ONE * (0.8 + 0.05 * float(y)))
				rows.append(Transform3D(basis, Vector3(-9.0 + 0.75 * float(x), -1.0 + float(y), -2.0 - 4.0 * float(z))))
	_rows = _multimesh(mesh, rows)
	_rows.transform = Transform3D(Basis(Vector3.UP, deg_to_rad(8.0)).scaled(Vector3.ONE * 1.1), Vector3(0.3, 0.0, -6.5))
	var hidden: Array[Transform3D] = []
	for y in 4:
		for x in 4:
			hidden.append(Transform3D(Basis(), Vector3(-1.2 + 0.8 * float(x), -1.2 + 0.8 * float(y), -9.0)))
	_hidden = _multimesh(mesh, hidden)
	_hidden_count = hidden.size()
	_camera = Camera3D.new()
	_camera.far = 100.0
	add_child(_camera)
	_camera.current = true
	_view(0)
	_run.call_deferred()


func _view(index: int) -> void:
	# 0: straight at the wall. 1: from above, looking over it.
	if index == 0:
		_camera.position = Vector3(0, 0, 0)
		_camera.look_at(Vector3(0, 0, -10))
	else:
		_camera.position = Vector3(1.5, 3.5, 1.0)
		_camera.look_at(Vector3(0, -0.5, -14))


func _occlusion(enabled: bool) -> void:
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/occlusion", enabled)


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


func _reference(index: int) -> Image:
	_occlusion(false)
	_view(index)
	await _settle()
	return await _frame()


func _taa_frame(enabled: bool) -> Image:
	# Turning TAA off and on drops its history; starting at the same place in
	# the 16-frame jitter sequence makes captures repeatable.
	_occlusion(enabled)
	_view(0)
	get_viewport().use_taa = false
	await RenderingServer.frame_post_draw
	while Engine.get_frames_drawn() % 16 != 0:
		await RenderingServer.frame_post_draw
	get_viewport().use_taa = true
	await _settle(48)
	return await _frame()


func _run() -> void:
	var references: Array[Image] = [await _reference(0), await _reference(1)]

	# A. Static views and alternating frames.
	_occlusion(true)
	var occluded := {}
	for index in 2:
		_view(index)
		await _settle()
		if (await _frame()).get_data() != references[index].get_data():
			_failures.append("view %d: image with occlusion differs from the reference" % index)
		var stats := await _statistics()
		occluded[index] = int(stats.get("hzb_instances_occluded", 0))
		if occluded[index] == 0:
			_failures.append("view %d: no instance occluded" % index)
		var per_lod: PackedInt64Array = stats.get("multimesh_instances_visible_per_lod", PackedInt64Array())
		var levels := 0
		for count in per_lod:
			levels += int(count > 0)
		if index == 1 and levels < 2:
			_failures.append("view 1: visible instances use %d LOD levels, expected several" % levels)
	var mismatches := 0
	for frame in 30:
		var index := frame % 2
		_view(index)
		if (await _frame()).get_data() != references[index].get_data():
			mismatches += 1
	if mismatches > 0:
		_failures.append("alternating: %d of 30 frames differ from the reference" % mismatches)

	# B. Only the hidden MultiMesh: fully inside the frustum, all occluded.
	_rows.visible = false
	for index in 2:
		# Negative control: equality above is only meaningful if visible rows
		# contribute pixels in both views.
		_view(index)
		await _settle()
		if (await _frame()).get_data() == references[index].get_data():
			_failures.append("view %d: the rows draw no visible instance, the comparison proves nothing" % index)
	_view(0)
	await _settle()
	var b := await _statistics()
	_rows.visible = true
	if int(b.get("hzb_instances_occluded", -1)) != _hidden_count or int(b.get("multimesh_surfaces_fully_visible", -1)) != 0:
		_failures.append("hidden MultiMesh: %s of %d instances occluded, %s surfaces kept their direct draw" % [b.get("hzb_instances_occluded"), _hidden_count, b.get("multimesh_surfaces_fully_visible")])

	# C. TAA.
	var taa_reference := await _taa_frame(false)
	var taa_repeat := await _taa_frame(false)
	var taa_occluded := await _taa_frame(true)
	var c := await _statistics()
	get_viewport().use_taa = false
	if taa_reference.get_data() != taa_repeat.get_data():
		_failures.append("TAA: two captures without occlusion differ, the comparison is not repeatable")
	elif taa_occluded.get_data() != taa_reference.get_data():
		_failures.append("TAA: image with occlusion differs from the reference")
	if int(c.get("hzb_instances_occluded", 0)) == 0:
		_failures.append("TAA: no instance occluded under jitter")

	# D. Setting off.
	_occlusion(false)
	await _settle()
	var d := await _statistics()
	if int(d.get("hzb_instances_occluded", -1)) != 0:
		_failures.append("setting off: %s instances occluded" % d.get("hzb_instances_occluded"))

	if _failures.is_empty():
		print("MULTIMESH_OCCLUSION_TEST_PASS: views occlude %d and %d instances with images equal to the reference, 30 alternating frames equal, hidden MultiMesh %d of %d occluded, TAA %d occluded and equal, none with the setting off" % [occluded[0], occluded[1], b.get("hzb_instances_occluded"), _hidden_count, c.get("hzb_instances_occluded")])
	else:
		for failure in _failures:
			printerr("MULTIMESH_OCCLUSION_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
