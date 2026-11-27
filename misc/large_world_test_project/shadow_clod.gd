extends Node3D
## Cluster LOD in shadow passes (rendering/geometry/cluster_lod/shadows).
## A field of dense CLOD meshes lit by a directional light with four
## cascades, a shadowed spot light and a shadowed omni light (cube mode).
##
## Default (devtools/test/large_world.ps1): images per view with shadow CLOD
## off (source), on with shadow_error_texels 0 (finest groups: must equal the
## source), and on with the default error (must stay close), plus a guard
## that the shadows are drawn at all. Prints SHADOW_CLOD_TEST_PASS.
## With the user argument --bench (devtools/test/shadow_clod_bench.ps1):
## GPU frame and shadow-stage times, source against CLOD, alternating order.

signal stages_ready(stages: Dictionary)

const GRID := 12
const SPACING := 8.0
const FRAMES := 240
const VIEWS: Array[String] = ["ground", "aerial"]
const MODES: Array[String] = ["source", "clod"]
var _camera: Camera3D
var _sun: DirectionalLight3D
var _spot: SpotLight3D
var _omni: OmniLight3D
var _objects := 0
var _source_triangles := 0
var _failed := false


func _dense_mesh() -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radius = 2.0
	sphere.height = 4.0
	sphere.radial_segments = 256
	sphere.rings = 128
	var mesh := ArrayMesh.new()
	var arrays := sphere.get_mesh_arrays()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	# A shadow mesh with reversed vertex numbering: source shadow passes draw
	# it, CLOD shadow passes must draw the source vertices with their groups'
	# indices (the finest-group image would differ otherwise).
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var reversed := PackedVector3Array()
	reversed.resize(positions.size())
	for vertex in positions.size():
		reversed[positions.size() - vertex - 1] = positions[vertex]
	var reversed_indices := PackedInt32Array()
	for index in arrays[Mesh.ARRAY_INDEX]:
		reversed_indices.append(positions.size() - index - 1)
	var shadow_arrays: Array = []
	shadow_arrays.resize(Mesh.ARRAY_MAX)
	shadow_arrays[Mesh.ARRAY_VERTEX] = reversed
	shadow_arrays[Mesh.ARRAY_INDEX] = reversed_indices
	var shadow_mesh := ArrayMesh.new()
	shadow_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, shadow_arrays)
	mesh.shadow_mesh = shadow_mesh
	if mesh.generate_cluster_lods() != OK:
		_fail("cluster LOD generation failed")
	return mesh


func _fail(message: String) -> void:
	_failed = true
	printerr("SHADOW_CLOD_TEST_FAIL: " + message)


func _ready() -> void:
	var bench := OS.get_cmdline_user_args().has("--bench")
	get_window().size = Vector2i(1920, 1080) if bench else Vector2i(960, 540)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", true)
	# Every cascade drawn every frame: a mode switch shows at once.
	ProjectSettings.set_setting("rendering/lights_and_shadows/directional_shadow/far_cascade_update_interval", 1)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.45, 0.55, 0.7)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.25, 0.25, 0.25)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	_sun = DirectionalLight3D.new()
	_sun.rotation_degrees = Vector3(-35, 40, 0)
	_sun.shadow_enabled = true
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	_sun.directional_shadow_max_distance = 160.0
	add_child(_sun)
	_spot = SpotLight3D.new()
	_spot.position = Vector3(-6, 14, 6)
	_spot.rotation_degrees = Vector3(-70, 0, 0)
	_spot.spot_range = 40.0
	_spot.spot_angle = 50.0
	_spot.light_energy = 6.0
	_spot.shadow_enabled = true
	add_child(_spot)
	_omni = OmniLight3D.new()
	_omni.position = Vector3(10, 6, -10)
	_omni.omni_range = 25.0
	_omni.light_energy = 4.0
	_omni.shadow_enabled = true
	_omni.omni_shadow_mode = OmniLight3D.SHADOW_CUBE
	add_child(_omni)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * (GRID + 4) * SPACING
	ground.mesh = plane
	add_child(ground)
	# One shared mesh and material: shadow passes must keep each instance a
	# separate element to give it its own cut.
	var dense := _dense_mesh()
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.8, 0.75, 0.7)
	var half := (GRID - 1) * 0.5
	for i in GRID:
		for j in GRID:
			var node := MeshInstance3D.new()
			node.mesh = dense
			node.material_override = material
			node.position = Vector3((i - half) * SPACING, 2.0 + float((i + j) % 3), (j - half) * SPACING)
			add_child(node)
			_objects += 1
	_source_triangles = _objects * dense.get_faces().size() / 3
	_camera = Camera3D.new()
	_camera.far = 400.0
	add_child(_camera)
	_camera.current = true
	if bench:
		_bench.call_deferred()
	else:
		_check.call_deferred()


func _view(index: int) -> void:
	var edge := GRID * 0.5 * SPACING
	if index == 0:
		_camera.position = Vector3(-edge - 4.0, 2.5, edge + 4.0)
		_camera.look_at(Vector3(edge, 0.0, -edge))
	else:
		_camera.position = Vector3(0, 70, 60)
		_camera.look_at(Vector3.ZERO)


func _mode(shadows: bool, error_texels: float) -> void:
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/shadows", shadows)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/shadow_error_texels", error_texels)


func _capture(frames: int) -> Image:
	for frame in frames:
		await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	image.convert(Image.FORMAT_RGBA8)
	return image


# [PSNR in dB, fraction of pixels with a channel off by more than p_threshold].
func _compare(a: Image, b: Image, p_threshold: int) -> Array[float]:
	var da := a.get_data()
	var db := b.get_data()
	if da.size() != db.size() or da.is_empty():
		return [0.0, 1.0]
	var squared := 0.0
	var differing := 0
	for p in range(0, da.size(), 4):
		var worst := 0
		for c in 3:
			var d := absi(da[p + c] - db[p + c])
			squared += d * d
			worst = maxi(worst, d)
		if worst > p_threshold:
			differing += 1
	var pixels := da.size() / 4
	var mse := squared / (pixels * 3.0)
	var psnr := 99.0 if mse == 0.0 else 10.0 * log(255.0 * 255.0 / mse) / log(10.0)
	return [psnr, float(differing) / pixels]


func _check() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	for view in VIEWS.size():
		_view(view)
		_mode(false, 1.0)
		var source := await _capture(90)
		_mode(true, 0.0)
		var exact := await _capture(60)
		_mode(true, 1.0)
		var clod := await _capture(60)
		_mode(false, 1.0)
		for light in [_sun, _spot, _omni]:
			light.shadow_enabled = false
		var unshadowed := await _capture(60)
		for light in [_sun, _spot, _omni]:
			light.shadow_enabled = true
		var exact_diff := _compare(source, exact, 2)
		var clod_diff := _compare(source, clod, 24)
		var shadow_diff := _compare(source, unshadowed, 24)
		print("SHADOW_CLOD_CHECK: %s view=%s exact_psnr=%.2f exact_differing=%.5f clod_psnr=%.2f clod_differing=%.5f shadowed_pixels=%.4f" % [driver, VIEWS[view], exact_diff[0], exact_diff[1], clod_diff[0], clod_diff[1], shadow_diff[1]])
		# The shadows must be there to compare at all.
		if shadow_diff[1] < 0.01:
			_fail("%s view=%s: shadows cover only %.4f of the image" % [driver, VIEWS[view], shadow_diff[1]])
		# The finest groups are the source triangles: the same depth.
		if exact_diff[1] > 0.0005:
			_fail("%s view=%s: finest-group shadows differ from the source (%.5f of the pixels)" % [driver, VIEWS[view], exact_diff[1]])
		if clod_diff[0] < 38.0 or clod_diff[1] > 0.003:
			_fail("%s view=%s: shadow CLOD at 1 texel too far from the source (PSNR %.2f, %.5f of the pixels)" % [driver, VIEWS[view], clod_diff[0], clod_diff[1]])
	if not _failed:
		print("SHADOW_CLOD_TEST_PASS: %s %d objects, %.1f M source triangles, directional, spot and omni cube shadows" % [driver, _objects, _source_triangles / 1.0e6])
	get_tree().quit(1 if _failed else 0)


func _read_stages() -> void:
	# Render thread. Timestamps of the frame that just retired; never blocks.
	var rd := RenderingServer.get_rendering_device()
	var times := {}
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if not times.has(name):
			times[name] = rd.get_captured_timestamp_gpu_time(i)
	var stages := {}
	var start: int = times.get("GPU Geometry Shadows", -1)
	var end: int = times.get("GPU Geometry Shadows End", -1)
	if start >= 0 and end >= start:
		stages.shadows = (end - start) / 1.0e6
	stages_ready.emit.call_deferred(stages)


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _bench() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	print("SHADOW_CLOD_BENCH_SCENE: %s %dx%d, %d CLOD objects (%.1f M source triangles), sun 4 cascades, spot, omni cube, %d frames per mode and repeat" % [driver, get_window().size.x, get_window().size.y, _objects, _source_triangles / 1.0e6, FRAMES])
	for view in VIEWS.size():
		_view(view)
		for warm in MODES.size():
			_mode(warm == 1, 1.0)
			for frame in 60:
				await RenderingServer.frame_post_draw
		var samples := {}
		for mode in MODES:
			samples[mode] = { "gpu": [] as Array[float], "shadows": [] as Array[float] }
		for repeat in 2:
			for k in MODES.size():
				var m := k if repeat == 0 else MODES.size() - 1 - k
				_mode(m == 1, 1.0)
				for frame in 30:
					await RenderingServer.frame_post_draw
				for frame in FRAMES:
					await RenderingServer.frame_post_draw
					samples[MODES[m]].gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
					RenderingServer.call_on_render_thread(_read_stages)
					var stages: Dictionary = await stages_ready
					for key in stages:
						samples[MODES[m]][key].append(stages[key])
		for mode in MODES:
			if _percentile(samples[mode].gpu, 0.5) <= 0.0 or _percentile(samples[mode].shadows, 0.5) <= 0.0:
				_fail("%s view=%s mode=%s has no GPU or shadow times" % [driver, VIEWS[view], mode])
			print("SHADOW_CLOD_BENCH: %s view=%s mode=%-6s gpu_p50=%.3f gpu_p95=%.3f shadows_p50=%.3f shadows_p95=%.3f" % [driver, VIEWS[view], mode, _percentile(samples[mode].gpu, 0.5), _percentile(samples[mode].gpu, 0.95), _percentile(samples[mode].shadows, 0.5), _percentile(samples[mode].shadows, 0.95)])
	get_tree().quit(1 if _failed else 0)
