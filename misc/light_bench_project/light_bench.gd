extends Node3D
## Many omni lights over a field of boxes (devtools/test/light_bench.ps1).
## Light counts grow in stages; each stage waits, then records the GPU and
## render CPU frame times and the opaque pass time (direct RD timestamps,
## present while a MultiMesh is GPU-culled). Not a correctness test.

signal stages_ready(stages: Dictionary)

const COUNTS: Array[int] = [0, 128, 512, 1024, 2048, 4096]
const FRAMES := 240
const FIELD := 200.0
var _lights: Array[OmniLight3D] = []
var _rng := RandomNumberGenerator.new()


func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", true)
	_rng.seed = 7
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.03)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.05, 0.05, 0.05)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * (FIELD + 40.0)
	ground.mesh = plane
	add_child(ground)
	var box := BoxMesh.new()
	box.size = Vector3(2, 3, 2)
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = box
	multimesh.instance_count = 1600
	for i in multimesh.instance_count:
		var position := Vector3((float(i % 40) - 19.5) * 5.0, 1.5, (float(i / 40) - 19.5) * 5.0)
		multimesh.set_instance_transform(i, Transform3D(Basis(), position))
	var boxes := MultiMeshInstance3D.new()
	boxes.multimesh = multimesh
	add_child(boxes)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 90, 110)
	camera.far = 500.0
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	_run.call_deferred()


func _add_lights(p_count: int) -> void:
	while _lights.size() < p_count:
		var light := OmniLight3D.new()
		light.position = Vector3(_rng.randf_range(-FIELD, FIELD) * 0.5, _rng.randf_range(0.5, 3.0), _rng.randf_range(-FIELD, FIELD) * 0.5)
		light.omni_range = 6.0
		light.light_energy = 1.0
		light.light_color = Color.from_hsv(_rng.randf(), 0.7, 1.0)
		add_child(light)
		_lights.append(light)


func _read_stages() -> void:
	var rd := RenderingServer.get_rendering_device()
	var times := {}
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if not times.has(name):
			times[name] = rd.get_captured_timestamp_gpu_time(i)
	var stages := {}
	var start: int = times.get("GPU Geometry Opaque Pass", -1)
	var end: int = times.get("GPU Geometry Opaque Pass End", -1)
	if start >= 0 and end >= start:
		stages.opaque = (end - start) / 1.0e6
	stages_ready.emit.call_deferred(stages)


# --dump-ts: every GPU timestamp of the last retired frame, with the time to
# the next one, to see where a pass's time goes.
func _dump_timestamps(p_count: int) -> void:
	var rd := RenderingServer.get_rendering_device()
	for i in rd.get_captured_timestamps_count() - 1:
		print("LIGHT_BENCH_TS: lights=%d %3d %.4f %s" % [p_count, i, (rd.get_captured_timestamp_gpu_time(i + 1) - rd.get_captured_timestamp_gpu_time(i)) / 1.0e6, rd.get_captured_timestamp_name(i)])
	stages_ready.emit.call_deferred({})


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _run() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	var limit: int = ProjectSettings.get_setting("rendering/limits/cluster_builder/max_clustered_elements")
	print("LIGHT_BENCH_SCENE: %s %dx%d, 1600 boxes, omni range 6, max_clustered_elements=%d, %d frames per count" % [driver, get_window().size.x, get_window().size.y, limit, FRAMES])
	for count in COUNTS:
		_add_lights(count)
		for frame in 60:
			await RenderingServer.frame_post_draw
		var gpu: Array[float] = []
		var cpu: Array[float] = []
		var opaque: Array[float] = []
		for frame in FRAMES:
			await RenderingServer.frame_post_draw
			gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
			cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid))
			RenderingServer.call_on_render_thread(_read_stages)
			var stages: Dictionary = await stages_ready
			if stages.has("opaque"):
				opaque.append(stages.opaque)
		if OS.get_cmdline_user_args().has("--dump-ts"):
			RenderingServer.call_on_render_thread(_dump_timestamps.bind(count))
			await stages_ready
		print("LIGHT_BENCH: %s limit=%d lights=%d gpu_p50=%.3f gpu_p95=%.3f cpu_p50=%.3f opaque_p50=%.3f" % [driver, limit, count, _percentile(gpu, 0.5), _percentile(gpu, 0.95), _percentile(cpu, 0.5), _percentile(opaque, 0.5)])
	get_tree().quit(0)
