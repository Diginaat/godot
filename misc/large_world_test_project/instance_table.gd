extends Node3D
## GPU instance table regression (devtools/test/instance_table.ps1). Turns on
## rendering/geometry/gpu_instance_table with its validation, then changes the
## scene in phases: add nodes past the first buffer size, move them, change
## layers and materials, free and add again (slot reuse and a second growth),
## teleport nodes (motion reset), render a second viewport, turn the table off
## and on (full rebuild). Every 30 frames the engine compares the GPU buffer
## with its CPU copy (GPU_INSTANCE_TABLE_CHECK) and reports how many drawn
## elements matched their records (GPU_INSTANCE_TABLE_ELEMENTS); the runner
## counts both. With `-- --self-test` one record is left stale and one upload
## dropped each frame, so both checks have to report a difference.

const PHASE_FRAMES := 90

var _nodes: Array[MeshInstance3D] = []
var _mesh := BoxMesh.new()
var _materials: Array[StandardMaterial3D] = []
var _moving := 0
var _stride := 7 # Moving nodes are spread (scatter path); 1 moves neighbours (direct path).
var _frame := 0


func _ready() -> void:
	var self_test := OS.get_cmdline_user_args().has("--self-test")
	ProjectSettings.set_setting("rendering/geometry/gpu_instance_table/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/gpu_instance_table/validate", 2 if self_test else 1)
	for i in 4:
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) * 0.25, 0.6, 0.9)
		_materials.append(material)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 40, 60)
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	_run.call_deferred(self_test)


func _add_second_view() -> void:
	# A second scene render in the same frame, seeing a different part of the
	# grid: its instances' motion age-out happens after the first view's upload.
	var viewport := SubViewport.new()
	viewport.size = Vector2i(320, 180)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.world_3d = get_world_3d()
	var camera := Camera3D.new()
	camera.position = Vector3(20, 6, 0)
	viewport.add_child(camera)
	add_child(viewport)
	camera.look_at(Vector3(10, 0, -20))
	camera.current = true


func _add(count: int) -> void:
	for i in count:
		var node := MeshInstance3D.new()
		node.mesh = _mesh
		node.material_override = _materials[_nodes.size() % _materials.size()]
		var index := _nodes.size()
		node.position = Vector3(float(index % 60) - 30.0, 0.5, float(index / 60) - 30.0)
		add_child(node)
		_nodes.append(node)


func _process(_delta: float) -> void:
	_frame += 1
	for i in mini(_moving, _nodes.size()):
		var node := _nodes[i * _stride % _nodes.size()]
		node.position.y = 0.5 + 0.5 * sin(float(_frame) * 0.1 + float(i))


func _wait(frames: int) -> void:
	for frame in frames:
		await RenderingServer.frame_post_draw


func _run(self_test: bool) -> void:
	_add(1500) # More than the 1024 records of the first buffer.
	await _wait(PHASE_FRAMES)
	print("INSTANCE_TABLE_PHASE: added %d" % _nodes.size())
	_moving = 200
	await _wait(PHASE_FRAMES)
	print("INSTANCE_TABLE_PHASE: moving 200 spread")
	_moving = 8
	_stride = 1
	await _wait(PHASE_FRAMES)
	print("INSTANCE_TABLE_PHASE: moving 8 adjacent")
	_moving = 200
	_stride = 7
	if not self_test:
		_moving = 0
		await _wait(4)
		for i in 100:
			var node := _nodes[i * 11 % _nodes.size()]
			node.position.x += 0.25
			RenderingServer.instance_teleport(node.get_instance())
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: teleported 100")
		_add_second_view()
		_moving = 200
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: second viewport")
		for i in 100:
			_nodes[i * 3].layers = 2
		for i in 50:
			_nodes[i * 5 + 1].material_override = _materials[(i + 1) % _materials.size()]
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: layers and materials")
		_moving = 0
		for i in 300:
			var node: MeshInstance3D = _nodes.pop_back()
			node.queue_free()
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: freed 300, %d left" % _nodes.size())
		_add(1000) # Reuses the 300 slots, then grows past 2048.
		_moving = 200
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: added again, %d" % _nodes.size())
		ProjectSettings.set_setting("rendering/geometry/gpu_instance_table/enabled", false)
		await _wait(PHASE_FRAMES)
		ProjectSettings.set_setting("rendering/geometry/gpu_instance_table/enabled", true)
		await _wait(PHASE_FRAMES)
		print("INSTANCE_TABLE_PHASE: off and on again")
	print("INSTANCE_TABLE_TEST_DONE: %d nodes" % _nodes.size())
	get_tree().quit(0)
