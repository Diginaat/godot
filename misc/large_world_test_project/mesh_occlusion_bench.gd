extends Node3D
## Frame time of HZB occlusion for CPU render list meshes
## (rendering/geometry/mesh_occlusion/*, roadmap G1 and G9). A town of 12 x 12
## walled courtyards; each courtyard holds 9 dense spheres (about 4 k
## triangles each) with a subsurface scattering material, which keeps them on
## the CPU list (the GPU instance path does not take SSS surfaces). From the
## street the walls hide the courtyards; from above they are open.
##
## The depth pre-pass already keeps hidden pixels from shading, so what
## occlusion saves here is vertex and raster work in the depth and colour
## passes, not material cost.
##
## Run: --path misc/large_world_test_project res://mesh_occlusion_bench.tscn
## Per view, occlusion off and on (two alternating repeats of FRAMES frames):
## GPU and render CPU p50, depth and opaque pass p50 (MESH_OCCLUSION_BENCH
## lines). Each view's images with occlusion off and on must be identical.

signal stages_ready(stages: Dictionary)

const BLOCKS := 12
const PITCH := 30.0
const FRAMES := 240
const VIEWS := ["street", "aerial"]
const MODES := ["off", "on"]

var _camera: Camera3D
var _props := 0
var _failed := false


func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.5, 0.6, 0.7)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	environment.ssao_enabled = true # A depth reader: a missing draw would show.
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * (BLOCKS * PITCH + 60.0)
	ground.mesh = plane
	add_child(ground)

	var wall_long := BoxMesh.new()
	wall_long.size = Vector3(24.0, 12.0, 1.0)
	var sphere := SphereMesh.new()
	sphere.radial_segments = 64
	sphere.rings = 32
	sphere.radius = 1.5
	sphere.height = 3.0
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.8, 0.4, 0.3)
	material.roughness = 0.4
	material.subsurf_scatter_enabled = true
	material.subsurf_scatter_strength = 0.5
	sphere.material = material
	var origin := -BLOCKS * PITCH * 0.5 + PITCH * 0.5
	for bx in BLOCKS:
		for bz in BLOCKS:
			var center := Vector3(origin + bx * PITCH, 0.0, origin + bz * PITCH)
			for side in 4:
				var wall := MeshInstance3D.new()
				wall.mesh = wall_long
				wall.rotation.y = side * PI * 0.5
				wall.position = center + Vector3(0, 6.0, 12.0).rotated(Vector3.UP, side * PI * 0.5)
				add_child(wall)
			for i in 9:
				var prop := MeshInstance3D.new()
				prop.mesh = sphere
				prop.position = center + Vector3((i % 3 - 1) * 6.0, 1.5, (i / 3 - 1) * 6.0)
				add_child(prop)
				_props += 1

	_camera = Camera3D.new()
	_camera.far = 800.0
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _view(index: int) -> void:
	match VIEWS[index]:
		"street":
			# Along the street between two rows of courtyards, at eye height.
			_camera.position = Vector3(0.0, 1.7, BLOCKS * PITCH * 0.5)
			_camera.look_at(Vector3(0.0, 1.7, -BLOCKS * PITCH * 0.5))
		"aerial":
			_camera.position = Vector3(0.0, 140.0, BLOCKS * PITCH * 0.45)
			_camera.look_at(Vector3(0.0, 0.0, 0.0))


func _mode(on: bool) -> void:
	ProjectSettings.set_setting("rendering/geometry/mesh_occlusion/enabled", on)


func _fail(message: String) -> void:
	_failed = true
	print("MESH_OCCLUSION_BENCH_FAIL: " + message)


func _capture(frames: int) -> Image:
	for frame in frames:
		await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	image.convert(Image.FORMAT_RGBA8)
	return image


func _statistics() -> Dictionary:
	var viewport := get_viewport().get_viewport_rid()
	var after := Engine.get_frames_drawn()
	for attempt in 90:
		await RenderingServer.frame_post_draw
		var result := RenderingServer.viewport_get_gpu_geometry_statistics(viewport)
		if not result.is_empty() and int(result.frame) > after + 1:
			return result
	return {}


func _read_stages() -> void:
	# Render thread. Timestamps of the frame that just retired; never blocks.
	var rd := RenderingServer.get_rendering_device()
	var times := {}
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if not times.has(name):
			times[name] = rd.get_captured_timestamp_gpu_time(i)
	var stages := {}
	for pair in [["depth", "GPU Geometry Depth Pre-Pass", "GPU Geometry Depth Pre-Pass End"], ["opaque", "GPU Geometry Opaque Pass", "GPU Geometry Opaque Pass End"]]:
		var start: int = times.get(pair[1], -1)
		var end: int = times.get(pair[2], -1)
		if start >= 0 and end >= start:
			stages[pair[0]] = (end - start) / 1.0e6
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
	print("MESH_OCCLUSION_BENCH_SCENE: %s %dx%d, %d CPU-list spheres in %d courtyards, %d frames per mode and repeat" % [driver, get_window().size.x, get_window().size.y, _props, BLOCKS * BLOCKS, FRAMES])
	for view in VIEWS.size():
		_view(view)
		_mode(false)
		var reference := await _capture(90)
		_mode(true)
		var occluded := await _capture(90)
		var statistics := await _statistics()
		print("MESH_OCCLUSION_BENCH_STATS: %s view=%s %s" % [driver, VIEWS[view], JSON.stringify(statistics)])
		if reference.get_data() != occluded.get_data():
			_fail("%s view=%s: the image with occlusion differs from the one without" % [driver, VIEWS[view]])
		var samples := {}
		for mode in MODES:
			samples[mode] = { "gpu": [] as Array[float], "cpu": [] as Array[float], "depth": [] as Array[float], "opaque": [] as Array[float] }
		for repeat in 2:
			for k in MODES.size():
				var m := k if repeat == 0 else MODES.size() - 1 - k
				_mode(m == 1)
				for frame in 30:
					await RenderingServer.frame_post_draw
				for frame in FRAMES:
					await RenderingServer.frame_post_draw
					samples[MODES[m]].gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
					samples[MODES[m]].cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid))
					RenderingServer.call_on_render_thread(_read_stages)
					var stages: Dictionary = await stages_ready
					for key in stages:
						samples[MODES[m]][key].append(stages[key])
		for mode in MODES:
			print("MESH_OCCLUSION_BENCH: %s view=%-6s mode=%-3s gpu_p50=%.3f gpu_p95=%.3f cpu_p50=%.3f depth_p50=%.3f opaque_p50=%.3f" % [driver, VIEWS[view], mode, _percentile(samples[mode].gpu, 0.5), _percentile(samples[mode].gpu, 0.95), _percentile(samples[mode].cpu, 0.5), _percentile(samples[mode].depth, 0.5), _percentile(samples[mode].opaque, 0.5)])
	get_tree().quit(1 if _failed else 0)
