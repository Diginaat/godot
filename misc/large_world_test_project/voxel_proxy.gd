extends Node3D
## VoxelProxyBuilder: exact voxel statistics for known inputs, determinism,
## and a distant render of a MultiMesh grove against its splat proxy
## (coverage and mean colour must agree within tolerances).

var _failures: Array[String] = []


func _plane(size: float, normal_up: bool, material: Material) -> ArrayMesh:
	var y := 0.0
	var vertices := PackedVector3Array([Vector3(0, y, 0), Vector3(size, y, 0), Vector3(size, y, size), Vector3(0, y, size)])
	var indices := PackedInt32Array([0, 2, 1, 0, 3, 2]) if normal_up else PackedInt32Array([0, 1, 2, 0, 2, 3])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	return mesh


func _near(a: Variant, b: Variant, tolerance: float) -> bool:
	if a is Vector3:
		return (a - b).length() <= tolerance
	if a is Color:
		return absf(a.r - b.r) <= tolerance and absf(a.g - b.g) <= tolerance and absf(a.b - b.b) <= tolerance
	return absf(a - b) <= tolerance


func _check_builder() -> void:
	var red := StandardMaterial3D.new()
	red.albedo_color = Color(1, 0, 0)
	var builder := VoxelProxyBuilder.new()
	builder.sample_spacing = 0.25
	# 1. One flat 1 m square filling one 1 m voxel: one splat of half size 0.5,
	# normal up, linear red albedo, centroid at the square's centre.
	builder.add_mesh(_plane(1.0, true, red), Transform3D(Basis(), Vector3(1, 0.5, 1)))
	var mesh := builder.build(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4)
	if mesh == null or mesh.surface_get_array_len(0) != 4:
		_failures.append("flat square: expected one splat")
		return
	var arrays := mesh.surface_get_arrays(0)
	var position: Vector3 = arrays[Mesh.ARRAY_VERTEX][0]
	var normal: Vector3 = arrays[Mesh.ARRAY_NORMAL][0]
	var color: Color = arrays[Mesh.ARRAY_COLOR][0]
	var half: float = arrays[Mesh.ARRAY_TEX_UV2][0].x
	if not _near(position, Vector3(1.5, 0.5, 1.5), 0.01) or not _near(normal, Vector3.UP, 0.01) or not _near(color, Color(1, 0, 0), 0.01) or not _near(half, 0.5, 0.01):
		_failures.append("flat square: position %s normal %s color %s half %s" % [position, normal, color, half])
	# 2. Two opposite faces in one voxel cancel: volumetric, lit from above.
	builder.clear()
	builder.add_mesh(_plane(1.0, true, red), Transform3D(Basis(), Vector3(1, 0.4, 1)))
	builder.add_mesh(_plane(1.0, false, red), Transform3D(Basis(), Vector3(1, 0.6, 1)))
	arrays = builder.build(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4).surface_get_arrays(0)
	if not _near(arrays[Mesh.ARRAY_NORMAL][0], Vector3.UP, 0.01):
		_failures.append("opposite faces: normal %s, expected up" % arrays[Mesh.ARRAY_NORMAL][0])
	# 3. Half coverage shrinks the splat by sqrt(0.5).
	builder.clear()
	builder.add_mesh(_plane(sqrt(0.5), true, red), Transform3D(Basis(), Vector3(1.1, 0.5, 1.1)))
	arrays = builder.build(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4).surface_get_arrays(0)
	if not _near(arrays[Mesh.ARRAY_TEX_UV2][0].x, 0.5 * sqrt(0.5), 0.02):
		_failures.append("half coverage: half size %s, expected %.3f" % [arrays[Mesh.ARRAY_TEX_UV2][0].x, 0.5 * sqrt(0.5)])
	# 4. Instance colours modulate vertex-colour materials; three instances in
	# three voxels. Repeated builds are identical.
	var tinted := StandardMaterial3D.new()
	tinted.vertex_color_use_as_albedo = true
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = _plane(1.0, true, tinted)
	multimesh.instance_count = 3
	for i in 3:
		multimesh.set_instance_transform(i, Transform3D(Basis(), Vector3(i, 0.5, 0)))
		multimesh.set_instance_color(i, [Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)][i])
	builder.clear()
	builder.add_multimesh(multimesh)
	var first := builder.build(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4)
	var second := builder.build(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4)
	if first.surface_get_array_len(0) != 12:
		_failures.append("multimesh: %d splat vertices, expected 12" % first.surface_get_array_len(0))
	else:
		var colors: PackedColorArray = first.surface_get_arrays(0)[Mesh.ARRAY_COLOR]
		for i in 3:
			if not _near(colors[i * 4], [Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)][i], 0.01):
				_failures.append("multimesh: voxel %d color %s" % [i, colors[i * 4]])
	if first.surface_get_arrays(0) != second.surface_get_arrays(0):
		_failures.append("repeated builds differ")


func _grove() -> MultiMesh:
	# Small boxes, randomly placed in a 40 m square: a stand-in for a far forest patch.
	var box := BoxMesh.new()
	box.size = Vector3(1.2, 4.0, 1.2)
	var material := StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	box.material = material
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = box
	multimesh.instance_count = 400
	var random := RandomNumberGenerator.new()
	random.seed = 5
	for i in 400:
		multimesh.set_instance_transform(i, Transform3D(Basis(Vector3.UP, random.randf() * TAU), Vector3(random.randf_range(-20, 20), 2.0, random.randf_range(-20, 20))))
		multimesh.set_instance_color(i, Color(random.randf_range(0.1, 0.3), random.randf_range(0.4, 0.7), 0.15))
	return multimesh


func _statistics(image: Image) -> Vector4:
	# Covered fraction and mean linear colour of covered pixels (sky is black).
	var covered := 0
	var sum := Color(0, 0, 0)
	for y in image.get_height():
		for x in image.get_width():
			var pixel := image.get_pixel(x, y)
			if pixel.get_luminance() > 0.01:
				covered += 1
				sum += pixel.srgb_to_linear()
	var n := maxf(covered, 1)
	return Vector4(float(covered) / (image.get_width() * image.get_height()), sum.r / n, sum.g / n, sum.b / n)


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	_check_builder()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 20, 0)
	add_child(sun)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 60, 220)
	camera.fov = 20
	add_child(camera)
	camera.look_at(Vector3(0, 2, 0))
	camera.current = true
	var grove := _grove()
	var source := MultiMeshInstance3D.new()
	source.multimesh = grove
	add_child(source)
	var builder := VoxelProxyBuilder.new()
	builder.sample_spacing = 0.5
	builder.add_multimesh(grove)
	var proxy := MeshInstance3D.new()
	proxy.mesh = builder.build(grove.get_aabb(), 24)
	add_child(proxy)
	_run.call_deferred(source, proxy)


func _capture(source: MultiMeshInstance3D, proxy: MeshInstance3D, show_proxy: bool) -> Image:
	source.visible = not show_proxy
	proxy.visible = show_proxy
	for frame in 10:
		await RenderingServer.frame_post_draw
	return get_viewport().get_texture().get_image()


func _run(source: MultiMeshInstance3D, proxy: MeshInstance3D) -> void:
	if proxy.mesh == null:
		_failures.append("grove proxy is empty")
	else:
		var triangles: int = grove_triangles(source.multimesh)
		var splats: int = proxy.mesh.surface_get_array_len(0) / 4
		var reference := _statistics(await _capture(source, proxy, false))
		var image := await _capture(source, proxy, true)
		var voxel := _statistics(image)
		image.save_png(ProjectSettings.globalize_path("res://../../devtools/out/large_world").path_join("voxel_proxy_%s.png" % RenderingServer.get_current_rendering_driver_name()))
		# A coverage-preserving proxy keeps the covered fraction and the colour.
		if reference.x < 0.02 or absf(voxel.x / reference.x - 1.0) > 0.25:
			_failures.append("coverage %.4f against %.4f for the source" % [voxel.x, reference.x])
		if absf(voxel.y - reference.y) > 0.08 or absf(voxel.z - reference.z) > 0.08 or absf(voxel.w - reference.w) > 0.08:
			_failures.append("mean colour %s against %s" % [Vector3(voxel.y, voxel.z, voxel.w), Vector3(reference.y, reference.z, reference.w)])
		if _failures.is_empty():
			print("VOXEL_PROXY_TEST_PASS: exact voxel statistics; grove of %d triangles as %d splats (%d triangles); coverage %.4f vs %.4f, mean colour %s vs %s" % [triangles, splats, splats * 2, voxel.x, reference.x, Vector3(voxel.y, voxel.z, voxel.w), Vector3(reference.y, reference.z, reference.w)])
	for failure in _failures:
		printerr("VOXEL_PROXY_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)


func grove_triangles(multimesh: MultiMesh) -> int:
	return multimesh.instance_count * multimesh.mesh.get_faces().size() / 3
