extends Node3D
## GPU path for eligible instances (devtools/test/gpu_instances.ps1). Turns on
## rendering/geometry/gpu_instances with its validation, then changes the
## scene in phases: moves, layers the camera does not see, hidden nodes,
## materials that leave the path (transparent) and come back, transparency,
## visibility ranges, meshes with index LODs and LOD biases, frees and adds, a second view of the same world and a
## view of another world. Every 30 frames the engine compares what the GPU
## path drew with what the CPU cull found visible (GPU_INSTANCES_CHECK). After
## each phase the image with the GPU path is compared with the image without
## it (GPU_INSTANCES_AB); they must be equal. With `-- --self-test` the engine
## leaves one draw bucket out, so both checks have to report a difference.

const PHASE_FRAMES := 60

var _nodes: Array[MeshInstance3D] = []
var _meshes: Array[Mesh] = []
var _materials: Array[StandardMaterial3D] = []
var _transparent := StandardMaterial3D.new()
var _moving := 0
var _frame := 0
var _ab_failures := 0


func _ready() -> void:
	var self_test := OS.get_cmdline_user_args().has("--self-test")
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/validate", 2 if self_test else 1)
	_meshes.append(BoxMesh.new())
	var sphere := SphereMesh.new()
	sphere.radial_segments = 16
	sphere.rings = 8
	_meshes.append(sphere)
	var cylinder := CylinderMesh.new()
	cylinder.radial_segments = 12
	_meshes.append(cylinder)
	for i in 4:
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) * 0.25, 0.6, 0.9)
		material.roughness = 0.3 + 0.15 * float(i)
		_materials.append(material)
	_transparent.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_transparent.albedo_color = Color(1, 1, 1, 0.5)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	add_child(sun)
	var omni := OmniLight3D.new()
	omni.position = Vector3(5, 3, 5)
	omni.omni_range = 12
	omni.light_color = Color(1, 0.6, 0.3)
	add_child(omni)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 30, 45)
	camera.cull_mask = 1 # Nodes moved to layer 2 are not visible.
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	_run.call_deferred(self_test)


func _add(count: int) -> void:
	for i in count:
		var node := MeshInstance3D.new()
		var index := _nodes.size()
		node.mesh = _meshes[index % _meshes.size()]
		node.material_override = _materials[(index / 3) % _materials.size()]
		node.position = Vector3(float(index % 60) - 30.0, 0.5, float(index / 60) - 30.0)
		node.rotation.y = float(index) * 0.37
		node.scale = Vector3.ONE * (0.6 + 0.4 * float(index % 5) / 4.0)
		add_child(node)
		_nodes.append(node)


func _process(_delta: float) -> void:
	_frame += 1
	for i in mini(_moving, _nodes.size()):
		var node := _nodes[i * 7 % _nodes.size()]
		node.position.y = 0.5 + 0.5 * sin(float(_frame) * 0.1 + float(i))


func _wait(frames: int) -> void:
	for frame in frames:
		await RenderingServer.frame_post_draw


func _capture() -> Image:
	await _wait(3)
	return get_viewport().get_texture().get_image()


# Same frame content with and without the GPU path: nothing may move.
func _compare(label: String) -> void:
	var moving := _moving
	_moving = 0
	var with_gpu := await _capture()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", false)
	var without_gpu := await _capture()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", true)
	await _wait(3)
	_moving = moving
	if with_gpu.get_data() == without_gpu.get_data():
		print("GPU_INSTANCES_AB: equal %s %dx%d" % [label, with_gpu.get_width(), with_gpu.get_height()])
		return
	var differing := 0
	var largest := 0.0
	for y in with_gpu.get_height():
		for x in with_gpu.get_width():
			var a := with_gpu.get_pixel(x, y)
			var b := without_gpu.get_pixel(x, y)
			if a != b:
				differing += 1
				largest = maxf(largest, maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b))))
	_ab_failures += 1
	printerr("GPU_INSTANCES_AB: differ %s: %d pixels, largest channel difference %.4f" % [label, differing, largest])


func _run(self_test: bool) -> void:
	_add(3000)
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: added %d" % _nodes.size())
	await _compare("static")
	if self_test:
		print("GPU_INSTANCES_TEST_DONE: self-test, %d image differences" % _ab_failures)
		get_tree().quit(0)
		return
	_moving = 200
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: moving 200")
	await _compare("moved")
	for i in 150:
		_nodes[i * 13 % _nodes.size()].layers = 2
	for i in 100:
		_nodes[i * 17 % _nodes.size() + 1].visible = false
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: layers and hidden")
	await _compare("layers and hidden")
	for i in 60:
		_nodes[i * 19 % _nodes.size()].material_override = _transparent
	for i in 40:
		_nodes[i * 23 % _nodes.size() + 2].transparency = 0.5
	for i in 40:
		_nodes[i * 29 % _nodes.size() + 3].visibility_range_end = 30.0
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: transparent, transparency, visibility range")
	await _compare("ineligible")
	for i in 60:
		_nodes[i * 19 % _nodes.size()].material_override = _materials[i % _materials.size()]
	for i in 40:
		_nodes[i * 23 % _nodes.size() + 2].transparency = 0.0
	for i in 100:
		_nodes[i * 17 % _nodes.size() + 1].visible = true
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: back to eligible")
	await _compare("eligible again")
	var lod_mesh := _lod_mesh()
	for i in range(0, _nodes.size(), 2):
		_nodes[i].mesh = lod_mesh
	# Biases from coarser to much finer, so that every index LOD is drawn.
	for i in 1000:
		_nodes[i * 2 % _nodes.size()].lod_bias = [0.5, 4.0, 16.0, 64.0, 256.0][i % 5]
	# Separate shadow meshes, which the depth pre-pass draws.
	var shadowed := _shadowed_meshes()
	for i in range(1, _nodes.size(), 4):
		_nodes[i].mesh = shadowed[(i / 4) % shadowed.size()]
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: index LODs and shadow meshes")
	await _compare("index LODs and shadow meshes")
	_moving = 0
	for i in 300:
		var node: MeshInstance3D = _nodes.pop_back()
		node.queue_free()
	await _wait(PHASE_FRAMES)
	_add(600)
	_moving = 200
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: freed 300, added 600, %d" % _nodes.size())
	await _compare("freed and added")
	_add_views()
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: second view and second world")
	await _compare("more views")
	print("GPU_INSTANCES_TEST_DONE: %d nodes, %d image differences" % [_nodes.size(), _ab_failures])
	get_tree().quit(0)


# A dense sphere with generated index LODs, so that the LOD chosen per instance
# changes with distance across the grid.
func _lod_mesh() -> Mesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = 96
	sphere.rings = 48
	var importer := ImporterMesh.new()
	importer.add_surface(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	importer.generate_lods(25.0, 60.0, [])
	print("GPU_INSTANCES_PHASE: LOD mesh with %d index LODs" % importer.get_surface_lod_count(0))
	return importer.get_mesh()


# Meshes with a separate shadow mesh: one whose shadow mesh has other index
# counts (a coarser sphere), and one with index LODs on both.
func _shadowed_meshes() -> Array[Mesh]:
	var fine := SphereMesh.new()
	fine.radial_segments = 32
	fine.rings = 16
	var coarse := SphereMesh.new()
	coarse.radial_segments = 10
	coarse.rings = 5
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, fine.get_mesh_arrays())
	var shadow := ArrayMesh.new()
	shadow.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, coarse.get_mesh_arrays())
	mesh.shadow_mesh = shadow
	var lod_mesh: ArrayMesh = _lod_mesh()
	lod_mesh.shadow_mesh = _lod_mesh()
	return [mesh, lod_mesh]


func _add_views() -> void:
	# A second view of the same world, and a view of a world of its own.
	var same := SubViewport.new()
	same.size = Vector2i(320, 180)
	same.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	same.world_3d = get_world_3d()
	var camera := Camera3D.new()
	camera.position = Vector3(20, 6, 0)
	same.add_child(camera)
	add_child(same)
	camera.look_at(Vector3(10, 0, -20))
	camera.current = true

	var other := SubViewport.new()
	other.size = Vector2i(320, 180)
	other.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	other.own_world_3d = true
	add_child(other)
	var other_camera := Camera3D.new()
	other_camera.position = Vector3(0, 8, 12)
	other.add_child(other_camera)
	other_camera.look_at(Vector3.ZERO)
	other_camera.current = true
	for i in 200:
		var node := MeshInstance3D.new()
		node.mesh = _meshes[i % _meshes.size()]
		node.material_override = _materials[i % _materials.size()]
		node.position = Vector3(float(i % 20) - 10.0, 0.5, float(i / 20) - 10.0)
		other.add_child(node)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-60, 20, 0)
	other.add_child(light)
