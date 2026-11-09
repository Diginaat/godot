extends Node3D
## Many-instance CPU benchmark (devtools/test/instance_bench.ps1). Baseline for
## the GPU-driven renderer work (docs/renderer/). Separate MeshInstance3D nodes,
## not MultiMesh, so every instance goes through scene culling, shadow culling,
## render-list building and draw submission on the CPU. Six cases, from a
## small-scene regression guard to 100k nodes, with moving instances and many
## lights. Each case has a static and an orbiting view; each is measured twice.
## Prints wall frame time, render CPU (viewport + frame setup), GPU time and
## draw counts as p50/p95/p99. With `--gpu-profile -- --stages` it instead
## prints the CPU time of each renderer stage (RENDER_TIMESTAMP markers).
## Not a correctness test.

const WARM_FRAMES := 90
const FRAMES := 300
const PROFILE_FRAMES := 60
const SPACING := 2.0

# name, nodes, meshes, materials (0 = one per node), moving nodes per frame, omni lights, shadowed spot lights
const CASES := [
	["small_1k", 1000, 1, 1, 0, 0, 0],
	["shared_20k", 20000, 4, 4, 0, 0, 0],
	["unique_10k", 10000, 1, 0, 0, 0, 0],
	["massive_100k", 100000, 1, 1, 0, 0, 0],
	["dynamic_20k", 20000, 4, 4, 2000, 0, 0],
	["lights_20k", 20000, 4, 4, 0, 128, 8],
]
const VIEWS: Array[String] = ["static", "orbit"]

signal stages_ready(stages: Dictionary)

var _camera: Camera3D
var _root: Node3D
var _moving: Array[MeshInstance3D] = []
var _bases: PackedVector3Array
var _extent := 0.0
var _frame := 0
var _orbit := false


func _ready() -> void:
	get_window().size = Vector2i(1280, 720)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.45, 0.55, 0.7)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 200.0
	add_child(sun)
	_camera = Camera3D.new()
	_camera.far = 800.0
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _meshes(count: int) -> Array[Mesh]:
	var meshes: Array[Mesh] = []
	for i in count:
		match i % 4:
			0:
				var box := BoxMesh.new()
				box.size = Vector3(1.0, 1.0, 1.0)
				meshes.append(box)
			1:
				var sphere := SphereMesh.new()
				sphere.radius = 0.6
				sphere.height = 1.2
				sphere.radial_segments = 16
				sphere.rings = 8
				meshes.append(sphere)
			2:
				var cylinder := CylinderMesh.new()
				cylinder.top_radius = 0.4
				cylinder.bottom_radius = 0.5
				cylinder.height = 1.4
				cylinder.radial_segments = 12
				meshes.append(cylinder)
			_:
				var prism := PrismMesh.new()
				prism.size = Vector3(1.0, 1.2, 1.0)
				meshes.append(prism)
	return meshes


func _material(index: int) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color.from_hsv(fmod(float(index) * 0.137, 1.0), 0.55, 0.85)
	return material


func _build(spec: Array) -> void:
	if _root:
		_root.free()
	_moving.clear()
	_root = Node3D.new()
	add_child(_root)
	var count: int = spec[1]
	var meshes := _meshes(spec[2])
	var materials: Array[StandardMaterial3D] = []
	for i in maxi(int(spec[3]), 1):
		materials.append(_material(i))
	var side := int(ceil(sqrt(float(count))))
	_extent = side * SPACING * 0.5
	_bases = PackedVector3Array()
	for i in count:
		var node := MeshInstance3D.new()
		node.mesh = meshes[i % meshes.size()]
		node.material_override = _material(i) if int(spec[3]) == 0 else materials[(i / meshes.size()) % materials.size()]
		var x := i % side
		var z := i / side
		# Deterministic jitter in height so the grid is not one flat layer.
		node.position = Vector3(x * SPACING - _extent, 0.5 + float((x * 7 + z * 13) % 5) * 0.4, z * SPACING - _extent)
		_root.add_child(node)
		if _moving.size() < int(spec[4]) and i % maxi(count / maxi(int(spec[4]), 1), 1) == 0:
			_moving.append(node)
			_bases.append(node.position)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * (_extent * 2.0 + 20.0)
	ground.mesh = plane
	_root.add_child(ground)
	var light_side := int(ceil(sqrt(float(spec[5]))))
	for i in int(spec[5]):
		var omni := OmniLight3D.new()
		omni.omni_range = 12.0
		omni.light_color = Color.from_hsv(fmod(float(i) * 0.21, 1.0), 0.6, 1.0)
		omni.position = Vector3((float(i % light_side) + 0.5) / light_side * 2.0 * _extent - _extent, 4.0, (float(i / light_side) + 0.5) / light_side * 2.0 * _extent - _extent)
		_root.add_child(omni)
	for i in int(spec[6]):
		var spot := SpotLight3D.new()
		spot.shadow_enabled = true
		spot.spot_range = 40.0
		spot.spot_angle = 40.0
		var angle := TAU * float(i) / float(spec[6])
		spot.position = Vector3(cos(angle) * _extent * 0.4, 18.0, sin(angle) * _extent * 0.4)
		_root.add_child(spot)
		spot.look_at(spot.position + Vector3(0.2, -1.0, 0.1))


func _view(orbit: bool) -> void:
	_orbit = orbit
	_frame = 0
	_place_camera()


func _place_camera() -> void:
	# Static: from one edge across the grid, about half of it in the frustum.
	# Orbit: same distance, turning 0.4 degrees per frame around the centre.
	var angle := deg_to_rad(0.4 * _frame) if _orbit else 0.0
	var radius := _extent + 10.0
	_camera.position = Vector3(sin(angle) * radius, 25.0, cos(angle) * radius)
	_camera.look_at(Vector3.ZERO)


func _process(_delta: float) -> void:
	_frame += 1
	if _orbit:
		_place_camera()
	# Frame-counted motion, so every run moves the same amount per frame.
	for i in _moving.size():
		var phase := float(_frame) * 0.05 + float(i) * 0.37
		_moving[i].position = _bases[i] + Vector3(sin(phase) * 0.5, 0.0, cos(phase) * 0.5)


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _measure(label: String) -> void:
	var rid := get_viewport().get_viewport_rid()
	var viewport := get_viewport()
	var samples := {"frame": [] as Array[float], "render_cpu": [] as Array[float], "gpu": [] as Array[float]}
	for frame in WARM_FRAMES:
		await RenderingServer.frame_post_draw
	var last := Time.get_ticks_usec()
	for frame in FRAMES:
		await RenderingServer.frame_post_draw
		var now := Time.get_ticks_usec()
		samples.frame.append((now - last) / 1000.0)
		last = now
		samples.render_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid) + RenderingServer.get_frame_setup_time_cpu())
		samples.gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
	var row := PackedStringArray()
	for key in ["frame", "render_cpu", "gpu"]:
		row.append("%s_p50=%.3f %s_p95=%.3f %s_p99=%.3f" % [key, _percentile(samples[key], 0.5), key, _percentile(samples[key], 0.95), key, _percentile(samples[key], 0.99)])
	row.append("draws=%d shadow_draws=%d objects=%d primitives=%d" % [
		viewport.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
		viewport.get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
		viewport.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_OBJECTS_IN_FRAME),
		viewport.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME)])
	row.append("video_mib=%.1f buffer_mib=%.1f static_mib=%.1f" % [
		Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0,
		Performance.get_monitor(Performance.RENDER_BUFFER_MEM_USED) / 1048576.0,
		Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0])
	if _percentile(samples.gpu, 0.5) <= 0.0 or _percentile(samples.render_cpu, 0.5) <= 0.0:
		printerr("INSTANCE_BENCH_FAIL: %s has no CPU or GPU times" % label)
	print("INSTANCE_BENCH: %s %s" % [label, " ".join(row)])


func _read_stages() -> void:
	# Render thread. CPU time of each marker until the next one, from the
	# frame that just retired; never blocks.
	var rd := RenderingServer.get_rendering_device()
	var sums := {}
	var count := rd.get_captured_timestamps_count()
	for i in count - 1:
		var name := rd.get_captured_timestamp_name(i)
		sums[name] = float(sums.get(name, 0.0)) + float(rd.get_captured_timestamp_cpu_time(i + 1) - rd.get_captured_timestamp_cpu_time(i)) / 1000.0
	stages_ready.emit.call_deferred(sums)


func _profile(label: String) -> void:
	# Only with --gpu-profile (which turns the RENDER_TIMESTAMP markers on),
	# in its own run: the markers add render-graph nodes and change ordering.
	# Every marker, with light/split/face numbers removed so that, e.g., all
	# directional splits add up into one stage.
	var stages := {}
	var digits := RegEx.create_from_string("[0-9]+")
	for frame in WARM_FRAMES:
		await RenderingServer.frame_post_draw
	for frame in PROFILE_FRAMES:
		await RenderingServer.frame_post_draw
		RenderingServer.call_on_render_thread(_read_stages)
		var sums: Dictionary = await stages_ready
		var merged := {}
		for name: String in sums:
			var key := digits.sub(name, "#", true)
			merged[key] = float(merged.get(key, 0.0)) + float(sums[name])
		for key: String in merged:
			if not stages.has(key):
				stages[key] = [] as Array[float]
			stages[key].append(merged[key])
	var row := PackedStringArray()
	for name: String in stages:
		# A stage missing in some frames counts as zero there.
		var values: Array[float] = stages[name]
		while values.size() < PROFILE_FRAMES:
			values.append(0.0)
		var p50 := _percentile(values, 0.5)
		if p50 >= 0.02:
			row.append("'%s'=%.3f" % [name, p50])
	if row.is_empty():
		printerr("INSTANCE_BENCH_FAIL: %s has no stage markers (run with --gpu-profile)" % label)
	print("INSTANCE_BENCH_STAGES: %s %s" % [label, " ".join(row)])


func _run() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var stages := OS.get_cmdline_user_args().has("--stages")
	print("INSTANCE_BENCH_SCENE: %s %dx%d, %d CPU threads, adapter '%s' driver '%s', %s" % [driver, get_window().size.x, get_window().size.y, OS.get_processor_count(), RenderingServer.get_video_adapter_name(), " ".join(OS.get_video_adapter_driver_info()), "stage profile, %d frames per view" % PROFILE_FRAMES if stages else "%d warm + %d timed frames per view, 2 repeats" % [WARM_FRAMES, FRAMES]])
	for repeat in 1 if stages else 2:
		for c in CASES.size():
			var spec: Array = CASES[c if repeat == 0 else CASES.size() - 1 - c]
			var started := Time.get_ticks_msec()
			_build(spec)
			await RenderingServer.frame_post_draw
			print("INSTANCE_BENCH_BUILD: %s nodes=%d build_ms=%d" % [spec[0], spec[1], Time.get_ticks_msec() - started])
			for v in VIEWS.size():
				_view(v == 1)
				var label := "%s case=%s view=%s repeat=%d" % [driver, spec[0], VIEWS[v], repeat]
				if stages:
					await _profile(label)
				else:
					await _measure(label)
	get_tree().quit(0)
