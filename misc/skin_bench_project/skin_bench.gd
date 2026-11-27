extends Node3D
## A crowd of skinned characters (devtools/test/skin_bench.ps1): 1000
## procedural tubes of 16 bones and about 4600 vertices each, posed every
## frame, on a 40 x 25 grid. Views: "all" sees the whole crowd, "few" sees
## about a tenth of it, "none" looks away. Records GPU frame, render CPU and
## the skinning compute time (direct RD timestamps). Not a correctness test.

signal stages_ready(stages: Dictionary)

const COUNT := 1000
const BONES := 16
const RINGS := 96
const SEGMENTS := 48
const FRAMES := 240
var _skeletons: Array[Skeleton3D] = []
var _camera: Camera3D
var _time := 0.0


func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.3, 0.35, 0.4)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	var mesh := _make_tube()
	for i in COUNT:
		var skeleton := Skeleton3D.new()
		for b in BONES:
			skeleton.add_bone("b%d" % b)
			if b > 0:
				skeleton.set_bone_parent(b, b - 1)
				skeleton.set_bone_rest(b, Transform3D(Basis(), Vector3(0, 0.25, 0)))
		skeleton.position = Vector3(float(i % 40) * 3.0 - 60.0, 0, -float(i / 40) * 3.0)
		add_child(skeleton)
		var instance := MeshInstance3D.new()
		instance.mesh = mesh
		skeleton.add_child(instance)
		instance.skeleton = NodePath("..")
		_skeletons.append(skeleton)
	_camera = Camera3D.new()
	_camera.far = 400.0
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _make_tube() -> ArrayMesh:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	var height := 0.25 * BONES
	for r in RINGS:
		var y := height * float(r) / float(RINGS - 1)
		var bone_f := clampf(y / 0.25, 0.0, float(BONES - 1))
		var b0 := int(floor(bone_f))
		var b1 := mini(b0 + 1, BONES - 1)
		var t := bone_f - float(b0)
		for s in SEGMENTS:
			var angle := TAU * float(s) / float(SEGMENTS)
			var n := Vector3(cos(angle), 0, sin(angle))
			vertices.append(n * 0.3 + Vector3(0, y, 0))
			normals.append(n)
			bones.append_array([b0, b1, 0, 0])
			weights.append_array([1.0 - t, t, 0.0, 0.0])
	for r in RINGS - 1:
		for s in SEGMENTS:
			var a := r * SEGMENTS + s
			var b := r * SEGMENTS + (s + 1) % SEGMENTS
			indices.append_array([a, a + SEGMENTS, b, b, a + SEGMENTS, b + SEGMENTS])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_BONES] = bones
	arrays[Mesh.ARRAY_WEIGHTS] = weights
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _process(delta: float) -> void:
	_time += delta
	for i in _skeletons.size():
		var skeleton := _skeletons[i]
		var phase := _time * 2.0 + float(i) * 0.37
		for b in range(1, BONES):
			skeleton.set_bone_pose_rotation(b, Quaternion(Vector3(0, 0, 1), sin(phase + float(b) * 0.4) * 0.12))


func _set_view(p_view: String) -> void:
	match p_view:
		"all":
			_camera.position = Vector3(0, 60, 40)
			_camera.look_at(Vector3(0, 0, -36))
		"few":
			_camera.position = Vector3(-56, 2.5, 8)
			_camera.look_at(Vector3(-56, 2.0, 0))
		"none":
			_camera.position = Vector3(0, 2.0, 20)
			_camera.look_at(Vector3(0, 2.0, 40))


func _read_stages() -> void:
	var rd := RenderingServer.get_rendering_device()
	var times := {}
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if not times.has(name):
			times[name] = rd.get_captured_timestamp_gpu_time(i)
	var stages := {}
	var start: int = times.get("Skinning", -1)
	var end: int = times.get("Skinning End", -1)
	if start >= 0 and end >= start:
		stages.skin = (end - start) / 1.0e6
	stages_ready.emit.call_deferred(stages)


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _run() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	print("SKIN_BENCH_SCENE: %s %dx%d, %d skinned tubes of %d bones and %d vertices, %d frames per view" % [driver, get_window().size.x, get_window().size.y, COUNT, BONES, RINGS * SEGMENTS, FRAMES])
	for view in ["all", "few", "none"]:
		_set_view(view)
		for frame in 60:
			await RenderingServer.frame_post_draw
		var gpu: Array[float] = []
		var cpu: Array[float] = []
		var skin: Array[float] = []
		var setup: Array[float] = []
		for frame in FRAMES:
			await RenderingServer.frame_post_draw
			gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
			cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid))
			setup.append(RenderingServer.get_frame_setup_time_cpu())
			RenderingServer.call_on_render_thread(_read_stages)
			var stages: Dictionary = await stages_ready
			if stages.has("skin"):
				skin.append(stages.skin)
		print("SKIN_BENCH: %s view=%s gpu_p50=%.3f gpu_p95=%.3f cpu_p50=%.3f setup_p50=%.3f skin_p50=%.3f skin_frames=%d" % [driver, view, _percentile(gpu, 0.5), _percentile(gpu, 0.95), _percentile(cpu, 0.5), _percentile(setup, 0.5), _percentile(skin, 0.5), skin.size()])
	get_tree().quit(0)
