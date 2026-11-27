extends Node3D
## Receiver culling of directional shadow casters
## (rendering/lights_and_shadows/directional_shadow/receiver_culling,
## kb shadow-receiver-culling): a city of 24 x 24 blocks, one building and
## 24 street props each (14,400 objects), under a sun with 4 cascades.
## Views: "street" (eye height down a street: buildings hide most of the
## city), "rooftop" (above the roofs, looking down at an angle) and "aerial"
## (high above: most casters shade something visible).
##
## Default: check mode, every cascade rendered every frame (cached ones are
## not culled). The image with receiver culling must match the one without
## in every view, also with a translucent pane over the street (a receiver
## the depth pre-pass does not hold) and with volumetric fog ("fog" view:
## the street with fog, which samples the shadow in front of surfaces)
## (prints SHADOW_RECEIVERS_TEST_PASS). --bench: GPU frame time,
## the GPU path's shadow stage and the receiver mask build, both modes in
## alternating order (SHADOW_RECEIVERS_BENCH lines). --views=a,b limits
## the views.

signal stages_ready(stages: Dictionary)

const BLOCKS := 24
const PITCH := 24.0 # Building 16 m wide, street 8 m.
const PROPS := 24
const FRAMES := 240
const VIEWS := ["street", "rooftop", "aerial", "fog"]
const MODES := ["off", "on"]

var _camera: Camera3D
var _sun: DirectionalLight3D
var _environment: Environment
var _objects := 0
var _failed := false


func _fail(message: String) -> void:
	_failed = true
	printerr("SHADOW_RECEIVERS_TEST_FAIL: " + message)


func _hash(i: int) -> float:
	# Deterministic [0, 1).
	var h := (i * 2654435761) & 0xffffffff
	h = ((h >> 16) ^ h) * 0x45d9f3b & 0xffffffff
	return float((h >> 8) & 0xffff) / 65536.0


func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var environment := Environment.new()
	_environment = environment
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.55, 0.65, 0.8)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.35, 0.38, 0.42)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)

	_sun = DirectionalLight3D.new()
	_sun.rotation_degrees = Vector3(-35, 40, 0)
	_sun.shadow_enabled = true
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	_sun.directional_shadow_max_distance = 300.0
	add_child(_sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(BLOCKS * PITCH + 200.0, BLOCKS * PITCH + 200.0)
	ground.mesh = plane
	add_child(ground)

	var building_mesh := BoxMesh.new()
	var building_material := StandardMaterial3D.new()
	building_material.albedo_color = Color(0.75, 0.72, 0.68)
	building_mesh.material = building_material
	var prop_mesh := SphereMesh.new()
	prop_mesh.radial_segments = 24
	prop_mesh.rings = 12
	var prop_material := StandardMaterial3D.new()
	prop_material.albedo_color = Color(0.3, 0.45, 0.3)
	prop_mesh.material = prop_material
	var half := BLOCKS * PITCH * 0.5
	for bz in BLOCKS:
		for bx in BLOCKS:
			var block := bz * BLOCKS + bx
			var center := Vector3(bx * PITCH - half + PITCH * 0.5, 0.0, bz * PITCH - half + PITCH * 0.5)
			var height := 8.0 + 52.0 * _hash(block)
			var building := MeshInstance3D.new()
			building.mesh = building_mesh
			building.position = center + Vector3(0, height * 0.5, 0)
			building.scale = Vector3(16.0, height, 16.0)
			add_child(building)
			_objects += 1
			# Props on the sidewalks around the building (trees, kiosks).
			for p in PROPS:
				var side := p % 4
				var along := (float(p / 4) + 0.5) / float(PROPS / 4) * 16.0 - 8.0
				var offset := Vector3(along, 0, 9.5) if side == 0 else Vector3(along, 0, -9.5) if side == 1 else Vector3(9.5, 0, along) if side == 2 else Vector3(-9.5, 0, along)
				var size := 1.0 + 1.5 * _hash(block * 97 + p)
				var prop := MeshInstance3D.new()
				prop.mesh = prop_mesh
				prop.position = center + offset + Vector3(0, size, 0)
				prop.scale = Vector3(size, size * 2.0, size)
				add_child(prop)
				_objects += 1

	# A translucent pane across the street ahead of the street view, in the
	# buildings' shadow and in front of the ground behind it.
	var street := -BLOCKS * PITCH * 0.5 + 6.0 * PITCH
	var pane := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(8.0, 6.0)
	var glass := StandardMaterial3D.new()
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass.albedo_color = Color(0.9, 0.9, 1.0, 0.5)
	glass.cull_mode = BaseMaterial3D.CULL_DISABLED
	quad.material = glass
	pane.mesh = quad
	pane.position = Vector3(street, 7.0, 200.0)
	add_child(pane)

	_camera = Camera3D.new()
	_camera.far = 800.0
	add_child(_camera)
	_camera.current = true
	if OS.get_cmdline_user_args().has("--bench"):
		_bench.call_deferred()
	else:
		_check.call_deferred()


func _view(index: int) -> void:
	# Streets run between blocks at multiples of PITCH from the city's edge.
	var street := -BLOCKS * PITCH * 0.5 + 6.0 * PITCH
	_environment.volumetric_fog_enabled = VIEWS[index] == "fog"
	_environment.volumetric_fog_density = 0.01
	match VIEWS[index]:
		"street", "fog":
			_camera.position = Vector3(street, 1.7, 250.0)
			_camera.look_at(Vector3(street + 20.0, 4.0, -100.0))
		"rooftop":
			_camera.position = Vector3(street, 75.0, 200.0)
			_camera.look_at(Vector3(street + 40.0, 0.0, 60.0))
		"aerial":
			_camera.position = Vector3(0.0, 260.0, 160.0)
			_camera.look_at(Vector3(0.0, 0.0, 0.0))


func _mode(culling: bool) -> void:
	ProjectSettings.set_setting("rendering/lights_and_shadows/directional_shadow/receiver_culling", culling)


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


func _shadow_objects() -> int:
	var rid := get_viewport().get_viewport_rid()
	return RenderingServer.viewport_get_render_info(rid, RenderingServer.VIEWPORT_RENDER_INFO_TYPE_SHADOW, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME)


func _check() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	ProjectSettings.set_setting("rendering/lights_and_shadows/directional_shadow/far_cascade_update_interval", 1)
	for view in VIEWS.size():
		_view(view)
		_mode(false)
		var reference := await _capture(90)
		var casters_off := _shadow_objects()
		_mode(true)
		var culled := await _capture(60)
		var casters_on := _shadow_objects()
		_mode(false)
		_sun.shadow_enabled = false
		var unshadowed := await _capture(60)
		_sun.shadow_enabled = true
		var diff := _compare(reference, culled, 2)
		var shadow_diff := _compare(reference, unshadowed, 24)
		print("SHADOW_RECEIVERS_CHECK: %s view=%s psnr=%.2f differing=%.6f shadowed_pixels=%.4f shadow_objects_off=%d shadow_objects_on=%d" % [driver, VIEWS[view], diff[0], diff[1], shadow_diff[1], casters_off, casters_on])
		if shadow_diff[1] < 0.01:
			_fail("%s view=%s: shadows cover only %.4f of the image" % [driver, VIEWS[view], shadow_diff[1]])
		# Conservative culling: the same shadows.
		if diff[1] > 0.0:
			_fail("%s view=%s: receiver culling changed %.6f of the pixels (PSNR %.2f)" % [driver, VIEWS[view], diff[1], diff[0]])
	if not _failed:
		print("SHADOW_RECEIVERS_TEST_PASS: %s %d objects, same image with receiver culling in %d views" % [driver, _objects, VIEWS.size()])
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
	for pair in [["shadows", "GPU Geometry Shadows", "GPU Geometry Shadows End"], ["receivers", "Shadow Receivers", "Shadow Receivers End"]]:
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


func _bench() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	print("SHADOW_RECEIVERS_BENCH_SCENE: %s %dx%d, %d objects, sun 4 cascades, %d frames per mode and repeat" % [driver, get_window().size.x, get_window().size.y, _objects, FRAMES])
	var views: Array = VIEWS.duplicate()
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--views="):
			views = argument.trim_prefix("--views=").split(",")
	for view in VIEWS.size():
		if not views.has(VIEWS[view]):
			continue
		_view(view)
		for warm in MODES.size():
			_mode(warm == 1)
			for frame in 60:
				await RenderingServer.frame_post_draw
		var samples := {}
		var casters := {}
		for mode in MODES:
			samples[mode] = { "gpu": [] as Array[float], "cpu": [] as Array[float], "shadows": [] as Array[float], "receivers": [] as Array[float] }
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
				casters[MODES[m]] = _shadow_objects()
		for mode in MODES:
			if _percentile(samples[mode].gpu, 0.5) <= 0.0 or _percentile(samples[mode].shadows, 0.5) <= 0.0:
				_fail("%s view=%s mode=%s has no GPU or shadow times" % [driver, VIEWS[view], mode])
			print("SHADOW_RECEIVERS_BENCH: %s view=%-7s mode=%-3s gpu_p50=%.3f gpu_p95=%.3f cpu_p50=%.3f shadows_p50=%.3f receivers_p50=%.3f shadow_objects=%d" % [driver, VIEWS[view], mode, _percentile(samples[mode].gpu, 0.5), _percentile(samples[mode].gpu, 0.95), _percentile(samples[mode].cpu, 0.5), _percentile(samples[mode].shadows, 0.5), _percentile(samples[mode].receivers, 0.5), casters[mode]])
	get_tree().quit(1 if _failed else 0)
