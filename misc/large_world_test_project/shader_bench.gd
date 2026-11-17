extends Node3D
## Shader and pipeline compilation benchmark (devtools/test/shader_bench.ps1).
## Starts with lights and shadows only, then adds COUNT objects with distinct
## StandardMaterial3D feature combinations (one shader each) in one frame and
## measures the stall of that frame, the time until frames are smooth again,
## the pipelines compiled per source, then steady-state CPU and GPU frame time.
## Run cold (--clear-shader-cache and no pipeline cache) and warm.
## User arguments: --count=N, --mv (TAA: motion vectors), --ssr (separate
## specular), --sss (subsurface scattering on some shaders), --dp (dual
## paraboloid omni shadows), --sdfgi, --voxelgi (baked at runtime), --shot=<png> (save the last frame, for image comparisons).

const WARM_FRAMES := 30
const SETTLE_FRAMES := 120
const STEADY_FRAMES := 300

var _count := 120
var _use_mv := false
var _use_ssr := false
var _shot := ""
var _use_sss := false
var _use_dp := false
var _use_sdfgi := false
var _use_voxelgi := false
var _frame := 0
var _phase := "warm"
var _last_usec := 0
var _spawn_usec := 0
var _spawn_frame_ms := 0.0
var _settle_ms := 0.0
var _max_ms := 0.0
var _frame_ms: Array[float] = []
var _cpu_ms: Array[float] = []
var _gpu_ms: Array[float] = []
var _first_frame_usec := 0
var _texture: ImageTexture


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--count="):
			_count = int(arg.get_slice("=", 1))
		elif arg == "--mv":
			_use_mv = true
		elif arg == "--ssr":
			_use_ssr = true
		elif arg == "--sss":
			_use_sss = true
		elif arg == "--dp":
			_use_dp = true
		elif arg == "--sdfgi":
			_use_sdfgi = true
		elif arg == "--voxelgi":
			_use_voxelgi = true
		elif arg.begins_with("--shot="):
			_shot = arg.get_slice("=", 1)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	get_window().size = Vector2i(1280, 720)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	if _use_mv:
		get_viewport().use_taa = true

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.4, 0.5, 0.65)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.35, 0.35, 0.4)
	environment.ssr_enabled = _use_ssr
	environment.sdfgi_enabled = _use_sdfgi
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 35, 0)
	sun.shadow_enabled = true
	add_child(sun)
	for i in 6:
		var omni := OmniLight3D.new()
		omni.position = Vector3(-15.0 + i * 6.0, 4.0, -8.0)
		omni.omni_range = 10.0
		omni.shadow_enabled = true
		if _use_dp:
			omni.omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
		omni.light_color = Color.from_hsv(i / 6.0, 0.4, 1.0)
		add_child(omni)
	for i in 3:
		var spot := SpotLight3D.new()
		spot.position = Vector3(-10.0 + i * 10.0, 8.0, -4.0)
		spot.rotation_degrees = Vector3(-70, 0, 0)
		spot.spot_range = 16.0
		spot.shadow_enabled = true
		add_child(spot)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(80, 80)
	ground.mesh = plane
	ground.position = Vector3(0, -1.2, -10)
	add_child(ground)

	var camera := Camera3D.new()
	add_child(camera)
	camera.look_at_from_position(Vector3(0, 6, 12), Vector3(0, 0, -10))

	var image := Image.create(64, 64, true, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			image.set_pixel(x, y, Color(0.8, 0.8, 0.8) if (x / 8 + y / 8) % 2 == 0 else Color(0.3, 0.3, 0.35))
	image.generate_mipmaps()
	_texture = ImageTexture.create_from_image(image)
	_last_usec = Time.get_ticks_usec()


# Feature combination `p_mask` as a StandardMaterial3D: each bit adds shader code.
func _material(p_mask: int) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color.from_hsv(float(p_mask % 97) / 97.0, 0.5, 0.9)
	if p_mask & 1:
		m.albedo_texture = _texture
	if p_mask & 2:
		m.normal_enabled = true
		m.normal_texture = _texture
	if p_mask & 4:
		m.emission_enabled = true
		m.emission = Color(0.2, 0.1, 0.0)
	if p_mask & 8:
		m.rim_enabled = true
	if p_mask & 16:
		m.clearcoat_enabled = true
	if p_mask & 32:
		m.anisotropy_enabled = true
	if p_mask & 64:
		m.ao_enabled = true
		m.ao_texture = _texture
	if p_mask & 128:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.albedo_color.a = 0.7
	elif p_mask & 256:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	if p_mask & 512:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if p_mask & 1024:
		m.vertex_color_use_as_albedo = true
	if p_mask & 2048:
		m.uv1_triplanar = true
	# Height mapping is ignored on triplanar materials (and warns).
	if p_mask & 4096 and not (p_mask & 2048):
		m.heightmap_enabled = true
		m.heightmap_texture = _texture
	if p_mask & 8192:
		m.backlight_enabled = true
	if p_mask & 16384:
		m.diffuse_mode = BaseMaterial3D.DIFFUSE_TOON
	if p_mask & 32768:
		m.detail_enabled = true
		m.detail_albedo = _texture
	# Subsurface scattering on every eighth shader: the separate specular pass.
	if _use_sss and p_mask % 8 == 3:
		m.subsurf_scatter_enabled = true
		m.subsurf_scatter_strength = 0.5
	return m


func _spawn() -> void:
	var mesh := SphereMesh.new()
	mesh.radius = 0.6
	mesh.height = 1.2
	mesh.radial_segments = 24
	mesh.rings = 12
	var columns := int(ceil(sqrt(_count * 2.0)))
	var used := {}
	var mask := 0
	for i in _count:
		# Distinct masks in a fixed pseudo-random order, the same in every run.
		mask = (mask * 1103515245 + 12345) & 0xFFFF
		while used.has(mask):
			mask = (mask + 1) & 0xFFFF
		used[mask] = true
		var instance := MeshInstance3D.new()
		instance.mesh = mesh
		instance.material_override = _material(mask)
		instance.position = Vector3((i % columns - columns * 0.5) * 1.5, 0.0, -float(i / columns) * 1.5)
		add_child(instance)
	if _use_voxelgi:
		var voxel_gi := VoxelGI.new()
		voxel_gi.size = Vector3(60, 12, 60)
		voxel_gi.position = Vector3(0, 2, -10)
		add_child(voxel_gi)
		voxel_gi.bake()


func _compilations() -> String:
	var names := ["canvas", "mesh", "surface", "draw", "specialization"]
	var infos := [
		RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS,
		RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH,
		RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE,
		RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW,
		RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION,
	]
	var parts: Array[String] = []
	for i in names.size():
		parts.append("pl_%s=%d" % [names[i], RenderingServer.get_rendering_info(infos[i])])
	return " ".join(parts)


func _median(p_values: Array[float]) -> float:
	var sorted := p_values.duplicate()
	sorted.sort()
	return sorted[sorted.size() / 2] if sorted.size() > 0 else 0.0


func _percentile(p_values: Array[float], p_fraction: float) -> float:
	var sorted := p_values.duplicate()
	sorted.sort()
	return sorted[mini(int(sorted.size() * p_fraction), sorted.size() - 1)] if sorted.size() > 0 else 0.0


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var frame_ms := (now - _last_usec) / 1000.0
	_last_usec = now
	_frame += 1
	if _first_frame_usec == 0:
		_first_frame_usec = now

	match _phase:
		"warm":
			if _frame >= WARM_FRAMES:
				print("SHADER_BENCH_PRE: %s" % _compilations())
				_phase = "settle"
				_frame = 0
				_spawn_usec = Time.get_ticks_usec()
				_spawn()
		"settle":
			# Frame 1 is the frame that drew the new objects first.
			if _frame == 1:
				_spawn_frame_ms = (now - _spawn_usec) / 1000.0
			_max_ms = maxf(_max_ms, frame_ms)
			_frame_ms.append(frame_ms)
			# Settled: the last frame longer than 50 ms after the spawn.
			if frame_ms > 50.0:
				_settle_ms = (now - _spawn_usec) / 1000.0
			if _frame >= SETTLE_FRAMES:
				_phase = "steady"
				_frame = 0
				_frame_ms.clear()
		"steady":
			var viewport := get_viewport().get_viewport_rid()
			_frame_ms.append(frame_ms)
			_cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(viewport) + RenderingServer.get_frame_setup_time_cpu())
			_gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(viewport))
			if _frame >= STEADY_FRAMES:
				print("SHADER_BENCH: count=%d mv=%d ssr=%d sss=%d dp=%d sdfgi=%d voxelgi=%d startup_ms=%.0f spawn_frame_ms=%.1f settle_ms=%.1f max_ms=%.1f frame_p50=%.2f frame_p99=%.2f cpu_p50=%.2f gpu_p50=%.2f mem_mb=%.1f mem_peak_mb=%.1f vram_mb=%.1f %s" % [
						_count, int(_use_mv), int(_use_ssr), int(_use_sss), int(_use_dp), int(_use_sdfgi), int(_use_voxelgi), _first_frame_usec / 1000.0, _spawn_frame_ms, _settle_ms, _max_ms,
						_median(_frame_ms), _percentile(_frame_ms, 0.99), _median(_cpu_ms), _median(_gpu_ms),
						OS.get_static_memory_usage() / 1048576.0, OS.get_static_memory_peak_usage() / 1048576.0,
						RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0, _compilations()])
				if not _shot.is_empty():
					get_viewport().get_texture().get_image().save_png(_shot)
				get_tree().quit()
