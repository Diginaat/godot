extends Node3D
## Game-level rendering benchmark for stock Godot vs this fork
## (devtools/test/stock_compare.ps1). Builds a procedural level from separate
## MeshInstance3D nodes (no MultiMesh): many meshes and materials with a
## skewed use count, a shadowed sun with four cascades, sky, SSAO and glow,
## omni and shadowed spot lights, moving props, and a camera that flies
## through the level. Everything is seeded and driven by the frame index, so
## both engines draw the same frames. Stock 4.7 API only.
##   godot --path <copy> res://level_bench.tscn -- --preset=town [--frames=600] [--warmup=120]
## Prints one LEVEL_BENCH line. Not a correctness test.

# name: nodes, meshes, materials, omni lights, shadowed spot lights, moving props, shadow distance
const PRESETS := {
	"town": [15000, 60, 300, 64, 4, 400, 150.0],
	"forest": [60000, 12, 24, 0, 0, 0, 300.0],
	"city_extreme": [200000, 120, 4000, 256, 16, 5000, 200.0],
}

var _preset := "town"
var _frames := 600
var _warmup := 120
var _spec: Array
var _side := 0.0
var _camera: Camera3D
var _moving: Array[MeshInstance3D] = []
var _moving_base: PackedVector3Array
var _frame := 0
var _last_usec := 0
var _build_ms := 0
var _first_frame_ms := 0.0
var _wall: Array[float] = []
var _cpu: Array[float] = []
var _gpu: Array[float] = []
var _draws: Array[float] = []
var _objects: Array[float] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--preset="):
			_preset = arg.get_slice("=", 1)
		elif arg.begins_with("--frames="):
			_frames = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--warmup="):
			_warmup = arg.get_slice("=", 1).to_int()
	if not PRESETS.has(_preset):
		print("LEVEL_BENCH_FAIL: unknown preset %s" % _preset)
		get_tree().quit(1)
		return
	_spec = PRESETS[_preset]
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	get_window().size = Vector2i(1280, 720)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var started := Time.get_ticks_msec()
	_build()
	_build_ms = Time.get_ticks_msec() - started
	_update_camera()
	_last_usec = Time.get_ticks_usec()
	RenderingServer.frame_post_draw.connect(_on_frame_post_draw)


func _build() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	var nodes: int = _spec[0]
	var mesh_count: int = _spec[1]
	var material_count: int = _spec[2]
	_side = sqrt(float(nodes)) * 4.0

	var environment := Environment.new()
	var sky_material := ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_material
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.tonemap_mode = Environment.TONE_MAPPER_ACES
	environment.ssao_enabled = true
	environment.glow_enabled = true
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = _spec[6]
	add_child(sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(_side * 1.2, _side * 1.2)
	ground.mesh = plane
	var ground_material := StandardMaterial3D.new()
	ground_material.albedo_color = Color(0.32, 0.36, 0.28)
	ground.material_override = ground_material
	add_child(ground)

	var image := Image.create(64, 64, true, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			image.set_pixel(x, y, Color(0.85, 0.85, 0.85) if (x / 8 + y / 8) % 2 == 0 else Color(0.45, 0.42, 0.4))
	image.generate_mipmaps()
	var texture := ImageTexture.create_from_image(image)

	var meshes: Array[Mesh] = []
	for i in mesh_count:
		meshes.append(_mesh(i, rng))
	var materials: Array[StandardMaterial3D] = []
	for i in material_count:
		var m := StandardMaterial3D.new()
		m.albedo_color = Color.from_hsv(rng.randf(), rng.randf_range(0.2, 0.7), rng.randf_range(0.4, 0.95))
		m.roughness = rng.randf_range(0.3, 1.0)
		m.metallic = 1.0 if i % 9 == 0 else 0.0
		if i % 5 == 0:
			m.albedo_texture = texture
		materials.append(m)

	var root := Node3D.new()
	add_child(root)
	var moving_count: int = _spec[5]
	for i in nodes:
		var node := MeshInstance3D.new()
		# Skewed use: a few meshes and materials are common, most are rare.
		node.mesh = meshes[int(pow(rng.randf(), 2.0) * mesh_count) % mesh_count]
		node.material_override = materials[int(pow(rng.randf(), 2.0) * material_count) % material_count]
		var p := Vector3(rng.randf_range(-0.5, 0.5) * _side, 0.0, rng.randf_range(-0.5, 0.5) * _side)
		node.position = p
		node.rotation.y = rng.randf() * TAU
		var s := rng.randf_range(0.6, 1.8)
		node.scale = Vector3(s, s * rng.randf_range(0.7, 2.5), s)
		root.add_child(node)
		if i < moving_count:
			_moving.append(node)
			_moving_base.append(p)

	for i in int(_spec[3]):
		var omni := OmniLight3D.new()
		omni.position = Vector3(rng.randf_range(-0.4, 0.4) * _side, 3.0, rng.randf_range(-0.4, 0.4) * _side)
		omni.omni_range = 10.0
		omni.light_color = Color.from_hsv(rng.randf(), 0.5, 1.0)
		add_child(omni)
	for i in int(_spec[4]):
		var spot := SpotLight3D.new()
		var a := TAU * i / float(_spec[4])
		spot.position = Vector3(cos(a), 0.0, sin(a)) * _side * 0.3 + Vector3(0, 9, 0)
		spot.rotation_degrees = Vector3(-75, 0, 0)
		spot.spot_range = 20.0
		spot.spot_angle = 40.0
		spot.shadow_enabled = true
		add_child(spot)

	_camera = Camera3D.new()
	_camera.far = 800.0
	add_child(_camera)


func _mesh(p_index: int, p_rng: RandomNumberGenerator) -> Mesh:
	match p_index % 6:
		0:
			var m := BoxMesh.new()
			m.size = Vector3(p_rng.randf_range(1, 4), p_rng.randf_range(1, 6), p_rng.randf_range(1, 4))
			return m
		1:
			var m := CylinderMesh.new()
			m.top_radius = p_rng.randf_range(0.1, 0.6)
			m.bottom_radius = p_rng.randf_range(0.3, 0.8)
			m.height = p_rng.randf_range(1, 5)
			m.radial_segments = 8 + p_index % 24
			return m
		2:
			var m := SphereMesh.new()
			m.radius = p_rng.randf_range(0.5, 1.5)
			m.height = m.radius * 2.0
			m.radial_segments = 16 + p_index % 32
			m.rings = 8 + p_index % 16
			return m
		3:
			var m := CapsuleMesh.new()
			m.radius = p_rng.randf_range(0.2, 0.6)
			m.height = p_rng.randf_range(1.5, 3.0)
			return m
		4:
			var m := PrismMesh.new()
			m.size = Vector3(p_rng.randf_range(1, 3), p_rng.randf_range(1, 3), p_rng.randf_range(1, 3))
			return m
		_:
			var m := TorusMesh.new()
			m.inner_radius = p_rng.randf_range(0.2, 0.5)
			m.outer_radius = m.inner_radius + p_rng.randf_range(0.3, 0.8)
			m.rings = 16 + p_index % 32
			return m


func _update_camera() -> void:
	# A slow figure-eight through the level at roof height, looking ahead.
	var t := _frame / 900.0 * TAU
	var r := _side * 0.3
	var pos := Vector3(sin(t) * r, 8.0 + 4.0 * sin(t * 3.0), sin(t * 2.0) * r * 0.5)
	var t2 := t + 0.05
	var ahead := Vector3(sin(t2) * r, 4.0, sin(t2 * 2.0) * r * 0.5)
	_camera.look_at_from_position(pos, ahead)


func _process(_delta: float) -> void:
	_frame += 1
	_update_camera()
	for i in _moving.size():
		var b := _moving_base[i]
		_moving[i].position = b + Vector3(sin(_frame * 0.05 + i), 0.5 + 0.5 * sin(_frame * 0.08 + i * 0.3), cos(_frame * 0.05 + i))
		_moving[i].rotation.y = _frame * 0.02 + i


func _on_frame_post_draw() -> void:
	var now := Time.get_ticks_usec()
	var wall := (now - _last_usec) / 1000.0
	_last_usec = now
	if _frame == 1:
		_first_frame_ms = wall
	if _frame <= _warmup:
		return
	var rid := get_viewport().get_viewport_rid()
	_wall.append(wall)
	_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid) + RenderingServer.get_frame_setup_time_cpu())
	_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
	_draws.append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME))
	_objects.append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME))
	if _wall.size() >= _frames:
		RenderingServer.frame_post_draw.disconnect(_on_frame_post_draw)
		_report()
		get_tree().quit()


func _pct(p_values: Array[float], p_q: float) -> float:
	var s := p_values.duplicate()
	s.sort()
	return s[clampi(int(round((s.size() - 1) * p_q)), 0, s.size() - 1)]


func _report() -> void:
	var gpu_instances = ProjectSettings.get_setting("rendering/geometry/gpu_instances/enabled", "n/a")
	print("LEVEL_BENCH: preset=%s nodes=%d engine=%s driver=%s adapter='%s' gpu_instances=%s build_ms=%d first_frame_ms=%.1f wall_p50=%.3f wall_p95=%.3f wall_p99=%.3f cpu_p50=%.3f cpu_p95=%.3f gpu_p50=%.3f gpu_p95=%.3f draws_p50=%d objects_p50=%d vram_mb=%.0f frames=%d" % [
		_preset, _spec[0], Engine.get_version_info().string, RenderingServer.get_current_rendering_driver_name(),
		RenderingServer.get_video_adapter_name(), gpu_instances, _build_ms, _first_frame_ms,
		_pct(_wall, 0.5), _pct(_wall, 0.95), _pct(_wall, 0.99), _pct(_cpu, 0.5), _pct(_cpu, 0.95),
		_pct(_gpu, 0.5), _pct(_gpu, 0.95), int(_pct(_draws, 0.5)), int(_pct(_objects, 0.5)),
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0, _wall.size()])
