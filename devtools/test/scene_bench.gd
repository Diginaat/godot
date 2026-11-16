extends SceneTree
## SceneTree / Node / transform benchmark (docs/benchmarks/scene-runtime.md).
##   godot --headless --path misc/scene_bench_project --script <this file> -- --scenario=<name> --count=N [--frames=F] [--warmup=W] [--visual]
## One scenario per process. Prints one SCENE_BENCH line: frame time (wall,
## between frames), the time of the scenario's own operations each frame,
## setup time and static memory. --visual uses MeshInstance3D instead of
## Node3D, so transform changes reach the RenderingServer (needs a window).

const SCENARIOS := ["idle", "deep", "wide", "parents", "sparse", "heavy", "script", "native", "churn", "churn_read", "reparent", "instance", "mixed", "heavy2d", "parents2d"]

var _scenario := "sparse"
var _count := 10000
var _frames := 120
var _warmup := 30
var _visual := false
var _rng := RandomNumberGenerator.new()
var _mesh: Mesh

var _frame := 0
var _last_usec := 0
var _frame_ms: Array[float] = []
var _op_ms: Array[float] = []
var _setup_ms := 0.0

var _container: Node3D
var _nodes: Array[Node3D] = []
var _roots: Array[Node3D] = []
var _leaves: Array[Node3D] = []
var _nodes_2d: Array[Node2D] = []
var _roots_2d: Array[Node2D] = []
var _parents: Array[Node] = []
var _scene: PackedScene
var _instances: Array[Node] = []
var _checksum := 0.0


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scenario="):
			_scenario = arg.get_slice("=", 1)
		elif arg.begins_with("--count="):
			_count = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--frames="):
			_frames = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--warmup="):
			_warmup = arg.get_slice("=", 1).to_int()
		elif arg == "--visual":
			_visual = true
	if not SCENARIOS.has(_scenario):
		printerr("SCENE_BENCH_FAIL: unknown scenario ", _scenario)
		quit(1)
		return
	Engine.max_fps = 0
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	_rng.seed = 12345
	_mesh = BoxMesh.new()
	_container = Node3D.new()
	_container.name = "Bench"
	root.add_child(_container)
	var start := Time.get_ticks_usec()
	call("_setup_" + _scenario)
	_setup_ms = (Time.get_ticks_usec() - start) / 1000.0


func _new_3d() -> Node3D:
	if _visual:
		var mesh_instance := MeshInstance3D.new()
		mesh_instance.mesh = _mesh
		return mesh_instance
	return Node3D.new()


# A tree of N nodes with branching 8 (breadth-first), so it is neither deep nor wide.
func _build_balanced(p_parent: Node3D, p_count: int, p_branching: int = 8) -> void:
	var frontier: Array[Node3D] = [p_parent]
	var made := 0
	while made < p_count:
		var next: Array[Node3D] = []
		for parent in frontier:
			for i in p_branching:
				if made >= p_count:
					break
				var n := _new_3d()
				n.position = Vector3(made % 100, 0, made / 100)
				parent.add_child(n)
				_nodes.append(n)
				next.append(n)
				made += 1
		frontier = next


func _setup_idle() -> void:
	_build_balanced(_container, _count)


func _setup_deep() -> void:
	# Chains of 100: each frame the chain roots move and the leaves are read.
	var chains := maxi(_count / 100, 1)
	for c in chains:
		var parent: Node3D = _container
		for d in 100:
			var n := _new_3d()
			n.position = Vector3(0.01, 0.1, 0)
			parent.add_child(n)
			if d == 0:
				_roots.append(n)
			parent = n
		_leaves.append(parent)


func _setup_wide() -> void:
	var parent := _new_3d()
	_container.add_child(parent)
	_roots.append(parent)
	for i in _count:
		var n := _new_3d()
		n.position = Vector3(i % 100, 0, i / 100)
		parent.add_child(n)
		_nodes.append(n)


func _setup_parents() -> void:
	# N / 100 parents with 100 children each; every parent moves every frame.
	for p in maxi(_count / 100, 1):
		var parent := _new_3d()
		_container.add_child(parent)
		_roots.append(parent)
		for i in 100:
			var n := _new_3d()
			n.position = Vector3(i, 0, 0)
			parent.add_child(n)
			_nodes.append(n)


func _setup_sparse() -> void:
	_build_balanced(_container, _count)


func _setup_heavy() -> void:
	_build_balanced(_container, _count)


func _setup_script() -> void:
	var script := GDScript.new()
	script.source_code = "extends Node3D\nvar t := 0.0\nfunc _process(delta: float) -> void:\n\tt += delta\n\tposition.y = sin(t)\n"
	script.reload()
	for i in _count:
		var n := Node3D.new()
		n.set_script(script)
		_container.add_child(n)
		_nodes.append(n)


func _setup_native() -> void:
	# Native processing: running timers (internal process every frame).
	for i in _count:
		var timer := Timer.new()
		timer.wait_time = 1000.0
		timer.autostart = true
		_container.add_child(timer)


func _setup_churn() -> void:
	# Leaves only (a freed node takes no listed children with it), under 10 parents.
	for p in 10:
		var parent := _new_3d()
		_container.add_child(parent)
		_parents.append(parent)
	for i in _count:
		var n := _new_3d()
		n.add_to_group("churned")
		_parents[i % 10].add_child(n)
		_nodes.append(n)


func _setup_churn_read() -> void:
	_setup_churn()


func _setup_reparent() -> void:
	for p in 2:
		var parent := _new_3d()
		_container.add_child(parent)
		_parents.append(parent)
	for i in _count:
		var n := _new_3d()
		_parents[i % 2].add_child(n)
		_nodes.append(n)


func _setup_instance() -> void:
	# A 100-node scene (with groups), instanced and freed every frame.
	var template := Node3D.new()
	for i in 99:
		var n := _new_3d()
		n.name = "Part%d" % i
		n.add_to_group("parts", true)
		template.add_child(n)
		n.owner = template
	_scene = PackedScene.new()
	_scene.pack(template)
	template.free()


func _setup_mixed() -> void:
	_build_balanced(_container, _count / 2)
	var canvas := Node2D.new()
	root.add_child(canvas)
	for i in _count - _count / 2:
		var n := Node2D.new()
		n.position = Vector2(i % 100, i / 100)
		canvas.add_child(n)
		_nodes_2d.append(n)


func _setup_heavy2d() -> void:
	# As heavy, with Node2D (CanvasItem propagation; added with M8).
	var canvas := Node2D.new()
	root.add_child(canvas)
	var frontier: Array[Node2D] = [canvas]
	var made := 0
	while made < _count:
		var next: Array[Node2D] = []
		for parent in frontier:
			for i in 8:
				if made >= _count:
					break
				var n := Node2D.new()
				n.position = Vector2(made % 100, made / 100)
				parent.add_child(n)
				_nodes_2d.append(n)
				next.append(n)
				made += 1
		frontier = next


func _setup_parents2d() -> void:
	# As parents, with Node2D under a few levels of containers (CanvasItem
	# propagation from a parent with many children; added with M8).
	var canvas := Node2D.new()
	root.add_child(canvas)
	var container: Node2D = canvas
	for level in 4:
		var inner := Node2D.new()
		container.add_child(inner)
		container = inner
	for p in maxi(_count / 100, 1):
		var parent := Node2D.new()
		container.add_child(parent)
		_roots_2d.append(parent)
		for i in 100:
			var n := Node2D.new()
			n.position = Vector2(i, 0)
			parent.add_child(n)
			_nodes_2d.append(n)


# Per-frame work; returns nothing, timed by the caller.
func _step() -> void:
	var t := _frame * 0.016
	match _scenario:
		"idle", "script", "native":
			pass
		"deep":
			for r in _roots:
				r.position.x = sin(t)
			for leaf in _leaves:
				_checksum += leaf.global_position.x
		"wide":
			_roots[0].position.y = sin(t)
			for i in range(0, _nodes.size(), 100):
				_checksum += _nodes[i].global_position.y
		"parents":
			for r in _roots:
				r.rotate_y(0.01)
		"sparse":
			for i in maxi(_count / 100, 1):
				_nodes[_rng.randi() % _nodes.size()].position.y = sin(t + i)
		"heavy":
			for n in _nodes:
				n.position.y = sin(t)
		"churn":
			# 2% of the nodes freed and replaced, each in a group.
			for i in maxi(_count / 50, 1):
				var index := _rng.randi() % _nodes.size()
				var old := _nodes[index]
				var parent := old.get_parent()
				old.free()
				var n := _new_3d()
				n.add_to_group("churned")
				parent.add_child(n)
				_nodes[index] = n
		"churn_read":
			# As churn, then one read of each parent's children (as get_index()
			# and the tree-order sort of call_group() do after removals).
			for i in maxi(_count / 50, 1):
				var index := _rng.randi() % _nodes.size()
				var old := _nodes[index]
				var parent := old.get_parent()
				old.free()
				var n := _new_3d()
				n.add_to_group("churned")
				parent.add_child(n)
				_nodes[index] = n
			for parent in _parents:
				_checksum += parent.get_child(0).get_index()
		"reparent":
			for i in maxi(_count / 100, 1):
				var n := _nodes[_rng.randi() % _nodes.size()]
				n.reparent(_parents[1] if n.get_parent() == _parents[0] else _parents[0])
		"instance":
			for inst in _instances:
				inst.free()
			_instances.clear()
			for i in maxi(_count / 100, 1):
				var inst := _scene.instantiate()
				_container.add_child(inst)
				_instances.append(inst)
		"heavy2d":
			for n in _nodes_2d:
				n.position.y = sin(t)
		"parents2d":
			for r in _roots_2d:
				r.rotate(0.01)
		"mixed":
			for i in maxi(_count / 200, 1):
				_nodes[_rng.randi() % _nodes.size()].position.y = sin(t + i)
				_nodes_2d[_rng.randi() % _nodes_2d.size()].position.y = sin(t + i)


func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	if _frame > 0 and _frame > _warmup:
		_frame_ms.append((now - _last_usec) / 1000.0)
	_last_usec = now
	_frame += 1
	var start := Time.get_ticks_usec()
	_step()
	if _frame > _warmup:
		_op_ms.append((Time.get_ticks_usec() - start) / 1000.0)
	if _frame_ms.size() >= _frames:
		print("SCENE_BENCH: scenario=%s count=%d visual=%s frames=%d frame_ms_p50=%.4f frame_ms_p95=%.4f op_ms_p50=%.4f setup_ms=%.2f static_mb=%.1f nodes=%d" % [
			_scenario, _count, _visual, _frames, _pct(_frame_ms, 0.5), _pct(_frame_ms, 0.95), _pct(_op_ms, 0.5), _setup_ms,
			OS.get_static_memory_usage() / 1048576.0, get_node_count()])
		quit()
		return true
	return false


func _pct(p_values: Array[float], p_p: float) -> float:
	var sorted := p_values.duplicate()
	sorted.sort()
	return sorted[clampi(int(p_p * (sorted.size() - 1)), 0, sorted.size() - 1)]
