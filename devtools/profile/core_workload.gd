extends SceneTree
## Core-heavy workloads for the sampling profiler. Run with:
##   godot --path <project> --script core_workload.gd -- --mode=<nodes|script|instance> [--seconds=N]
## Prints CORE_WORKLOAD lines with frames and ms per frame.

var _mode := "nodes"
var _seconds := 25.0
var _frames := 0
var _start := 0
var _nodes: Array[Node3D] = []
var _mesh: Mesh
var _scene: PackedScene
var _logic: Array = []


class Agent:
	extends RefCounted
	var stats := {"health": 100, "speed": 3.5, "name": "agent"}
	var position := Vector3.ZERO
	var velocity := Vector3.ZERO
	var path: Array[Vector3] = []
	var tags: Array[String] = []

	func step(p_delta: float, p_target: Vector3, p_frame: int) -> String:
		var to := p_target - position
		if to.length_squared() > 0.01:
			velocity = velocity.lerp(to.normalized() * float(stats["speed"]), 0.1)
		position += velocity * p_delta
		path.append(position)
		if path.size() > 16:
			path.pop_front()
		stats["health"] = int(stats["health"]) - (1 if p_frame % 7 == 0 else 0)
		if stats["health"] <= 0:
			stats["health"] = 100
		var key := "k%d" % (p_frame % 32)
		stats[key] = stats.get(key, 0) + 1
		if tags.size() < 8:
			tags.append(str(p_frame))
		return "%s:%d" % [stats["name"], stats["health"]]


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--mode="):
			_mode = arg.get_slice("=", 1)
		elif arg.begins_with("--seconds="):
			_seconds = arg.get_slice("=", 1).to_float()
	# Frame time is CPU time, not the display's refresh.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	var camera := Camera3D.new()
	camera.position = Vector3(0, 40, 120)
	root.add_child(camera)
	_mesh = BoxMesh.new()
	match _mode:
		"nodes":
			for i in 10000:
				_nodes.append(_add_node(i))
		"script":
			for i in 2000:
				var agent := Agent.new()
				agent.position = Vector3(i % 50, 0, i / 50)
				_logic.append(agent)
		"instance":
			var template := Node3D.new()
			for i in 2000:
				var child := MeshInstance3D.new()
				child.mesh = _mesh
				child.name = "Part%d" % i
				child.position = Vector3(i % 40, 0, i / 40)
				template.add_child(child)
				child.owner = template
			_scene = PackedScene.new()
			_scene.pack(template)
			template.free()
	_start = Time.get_ticks_usec()
	print("CORE_WORKLOAD_START: mode=", _mode)


func _add_node(p_index: int) -> Node3D:
	var node := MeshInstance3D.new()
	node.mesh = _mesh
	node.position = Vector3(p_index % 100, 0, p_index / 100)
	node.add_to_group("movers" if p_index % 2 == 0 else "others")
	root.add_child(node)
	return node


func _process(p_delta: float) -> bool:
	_frames += 1
	var t := _frames * 0.016
	match _mode:
		"nodes":
			for i in range(_frames % 5, _nodes.size(), 5):
				var node := _nodes[i]
				node.position.y = sin(t + i * 0.01)
				node.rotate_y(0.01)
			for i in 200:
				var index := (_frames * 200 + i) % _nodes.size()
				_nodes[index].queue_free()
				_nodes[index] = _add_node(index)
			call_group("others", "set_visible", _frames % 2 == 0)
		"script":
			var target := Vector3(sin(t) * 20.0, 0, cos(t) * 20.0)
			var log := PackedStringArray()
			for agent in _logic:
				log.append((agent as Agent).step(0.016, target, _frames))
			var joined := ",".join(log.slice(0, 64))
			if joined.length() == 0:
				print("empty")
		"instance":
			var instance := _scene.instantiate()
			root.add_child(instance)
			instance.queue_free()
	var elapsed := (Time.get_ticks_usec() - _start) / 1.0e6
	if elapsed >= _seconds:
		print("CORE_WORKLOAD: mode=%s frames=%d ms_per_frame=%.3f" % [_mode, _frames, elapsed * 1000.0 / _frames])
		quit()
		return true
	return false
