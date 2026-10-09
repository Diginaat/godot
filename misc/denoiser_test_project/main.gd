extends Node3D
## Test scenes for the native path tracer denoiser (native ray
## reconstruction). Builds everything from code. See
## docs/renderer/native_ray_reconstruction.md in the repository root.
##
## Every view is a pure function of the simulation time, so a run is
## reproducible frame by frame: the denoised sequence and the reference
## sequence (same frames, converged without denoising) line up exactly.
##
## Views:
##   cornell   closed box lit by an emissive ceiling panel, static camera
##   pan       pillars and a thin railing, fast camera pan (disocclusion)
##   emissive  dark room, a bright emissive ball circling (ghost trails)
##   skinned   bending skinned tentacles (deformed geometry, motion vectors)
##   thin      thin fence bars and alpha scissor leaves, camera strafing
##   mirror    mirror and glossy floors, rough metal, a moving box; orbiting camera
##   lights    moving colored lights and a sweeping spot light, moving shadows
##   dark      dark room, one small bright light (high contrast)

const AREA_SPACING := 60.0
const VIEW_KEYS := ["cornell", "pan", "emissive", "skinned", "thin", "mirror", "lights", "dark"]
const DT := 1.0 / 60.0

var args := {}
var env: Environment
var camera: Camera3D
var vp: SubViewport
var hud: Label
var view := "cornell"
var sim_time := 0.0
var frozen := false
var frame := 0
var animated: Array[Callable] = []
var capturing := false
# Fly camera (F, or --fly): right mouse looks, WASD moves, Q/E down/up,
# Shift faster, mouse wheel changes the speed. The scene keeps animating.
var flying := false
var fly_yaw := 0.0
var fly_pitch := 0.0
var fly_speed := 3.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts: PackedStringArray = arg.trim_prefix("--").split("=", true, 1)
		args[parts[0]] = parts[1] if parts.size() > 1 else "1"
	capturing = args.has("shot") or args.has("sequence") or args.has("bench")

	# Always render into an offscreen viewport of a fixed size, so captures
	# don't depend on the window or the screen.
	vp = SubViewport.new()
	var res: PackedStringArray = String(args.get("res", "1280x720")).split("x")
	vp.size = Vector2i(int(res[0]), int(res[1]))
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(vp)
	var rect := TextureRect.new()
	rect.texture = vp.get_texture()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(rect)

	var root := Node3D.new()
	vp.add_child(root)
	_build_environment(root)
	for i in VIEW_KEYS.size():
		var area := Node3D.new()
		area.name = VIEW_KEYS[i]
		area.position = Vector3(i * AREA_SPACING, 0, 0)
		root.add_child(area)
		call("_build_" + VIEW_KEYS[i], area)

	camera = Camera3D.new()
	camera.fov = 60.0
	camera.far = 300.0
	root.add_child(camera)

	hud = Label.new()
	hud.position = Vector2(12, 8)
	hud.add_theme_color_override("font_outline_color", Color.BLACK)
	hud.add_theme_constant_override("outline_size", 6)
	var layer := CanvasLayer.new()
	layer.add_child(hud)
	add_child(layer)

	_apply_args()
	_update_scene()
	if args.has("fly") and not capturing:
		_set_flying(true)
	if capturing:
		hud.visible = false
		_run_capture()


func _apply_args() -> void:
	view = args.get("view", "cornell")
	if not VIEW_KEYS.has(view):
		push_error("Unknown view '%s'. Known: %s" % [view, ", ".join(VIEW_KEYS)])
		view = "cornell"
	env.pathtracing_enabled = args.get("pt", "1") == "1"
	env.pathtracing_samples_per_pixel = int(args.get("spp", "1"))
	env.pathtracing_max_bounces = int(args.get("bounces", "3"))
	env.pathtracing_denoiser = int(args.get("denoiser", "2"))
	env.pathtracing_debug_mode = int(args.get("pt_debug", "0"))
	_set_rr_debug(int(args.get("rr_debug", "0")))
	if args.get("tonemap", "agx") == "linear":
		# Measurements: linear output, nothing clips at the default exposure.
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		env.tonemap_exposure = float(args.get("exposure", "0.5"))
	if args.has("scale3d"):
		# 0 bilinear, 1 FSR1, 2 FSR2, 6 DLSS (see Viewport.Scaling3DMode).
		vp.scaling_3d_mode = int(args["scale3d"])
		vp.scaling_3d_scale = float(args.get("scale", "0.67"))
	if args.has("taa"):
		vp.use_taa = args["taa"] == "1"


func _set_rr_debug(p_mode: int) -> void:
	ProjectSettings.set_setting("rendering/ray_reconstruction/debug_mode", p_mode)


func _process(delta: float) -> void:
	if not frozen:
		sim_time += DT if capturing else delta
		frame += 1
	_update_scene()
	if flying:
		_fly(delta)
	if not capturing:
		var fly_help := "   fly speed %.1f: RMB look, WASD move, Q/E down/up, Shift fast, wheel speed" % fly_speed if flying else ""
		hud.text = "view: %s   denoiser: %d   debug: %d   t=%.1f%s\n1-8 views   R denoiser   V debug view   P path tracing   F fly camera" % [
			view, env.pathtracing_denoiser, int(ProjectSettings.get_setting("rendering/ray_reconstruction/debug_mode")), sim_time, fly_help]


# Camera and animation for the current simulation time.
func _update_scene() -> void:
	var t := sim_time
	for f in animated:
		f.call(t)
	var origin := Vector3(VIEW_KEYS.find(view) * AREA_SPACING, 0, 0)
	var eye := Vector3.ZERO
	var target := Vector3.ZERO
	match view:
		"cornell":
			eye = Vector3(0, 1.5, 3.6)
			target = Vector3(0, 1.4, 0)
		"pan":
			eye = Vector3(0, 1.6, 7)
			var yaw := sin(t * 1.2) * 0.7
			target = eye + Vector3(sin(yaw), -0.1, -cos(yaw))
		"emissive":
			eye = Vector3(0, 2.0, 3.8)
			target = Vector3(0, 0.8, 0)
		"skinned":
			var a := t * 0.2
			eye = Vector3(sin(a) * 6.0, 2.5, cos(a) * 6.0)
			target = Vector3(0, 1.5, 0)
		"thin":
			eye = Vector3(sin(t * 0.5) * 3.0, 1.4, 4.5)
			target = eye + Vector3(0, -0.05, -1)
		"mirror":
			var a := t * 0.3
			eye = Vector3(sin(a) * 7.0, 2.2, cos(a) * 7.0)
			target = Vector3(0, 0.6, 0)
		"lights":
			eye = Vector3(0, 4.0, 9.0)
			target = Vector3(0, 0.5, 0)
		"dark":
			eye = Vector3(sin(t * 0.3) * 1.5, 1.6, 2.5)
			target = Vector3(0, 1.0, -1.0)
	# --teleport=N: every N frames the camera cuts to the opposite side.
	if args.has("teleport") and (frame / int(args["teleport"])) % 2 == 1:
		eye = Vector3(-eye.x, eye.y, -eye.z)
		target = Vector3(-target.x, target.y, -target.z)
	if not flying:
		camera.look_at_from_position(origin + eye, origin + target)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode >= KEY_1 and key.keycode <= KEY_8:
		view = VIEW_KEYS[key.keycode - KEY_1]
		if flying:
			# Jump to the view's own camera, then keep flying from there.
			_set_flying(false)
			_update_scene()
			_set_flying(true)
	elif key.keycode == KEY_F:
		_set_flying(not flying)
	elif key.keycode == KEY_R:
		env.pathtracing_denoiser = 2 if env.pathtracing_denoiser == 0 else 0
	elif key.keycode == KEY_V:
		_set_rr_debug((int(ProjectSettings.get_setting("rendering/ray_reconstruction/debug_mode")) + 1) % 9)
	elif key.keycode == KEY_P:
		env.pathtracing_enabled = not env.pathtracing_enabled


func _unhandled_input(event: InputEvent) -> void:
	if not flying:
		return
	var button := event as InputEventMouseButton
	if button:
		if button.button_index == MOUSE_BUTTON_RIGHT:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if button.pressed else Input.MOUSE_MODE_VISIBLE
		elif button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_UP:
			fly_speed = minf(fly_speed * 1.25, 100.0)
		elif button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			fly_speed = maxf(fly_speed / 1.25, 0.1)
	var motion := event as InputEventMouseMotion
	if motion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		fly_yaw -= motion.relative.x * 0.003
		fly_pitch = clampf(fly_pitch - motion.relative.y * 0.003, -1.5, 1.5)
		camera.rotation = Vector3(fly_pitch, fly_yaw, 0.0)


func _set_flying(p_on: bool) -> void:
	flying = p_on
	if flying:
		# Start from where the view's camera is now.
		fly_yaw = camera.rotation.y
		fly_pitch = camera.rotation.x
		camera.rotation = Vector3(fly_pitch, fly_yaw, 0.0)
	else:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _fly(p_delta: float) -> void:
	var dir := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		dir.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		dir.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		dir.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		dir.x += 1.0
	var up := 0.0
	if Input.is_physical_key_pressed(KEY_E):
		up += 1.0
	if Input.is_physical_key_pressed(KEY_Q):
		up -= 1.0
	var speed := fly_speed * (4.0 if Input.is_physical_key_pressed(KEY_SHIFT) else 1.0)
	var move := camera.global_transform.basis * dir.normalized() + Vector3.UP * up
	camera.global_position += move * speed * p_delta


# --- Capture -------------------------------------------------------------------

# --frames=N warm-up frames, then one of:
#   --shot=path.png            save one image
#   --sequence=N --out=dir     save N consecutive frames as dir/<view>_NNN.png;
#                              with --reference=K every frame is instead the
#                              average of K frames with the scene frozen
#                              (debug mode Reference), i.e. converged
#   --bench=N                  print GPU times over N frames (needs --gpu-profile)
# Stability: --resize_at=F:WxH resizes the viewport at frame F,
# --scale_at=F:MODE switches the 3D scaling mode, --cycles=N creates and frees
# N extra path traced viewports first.
func _run_capture() -> void:
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	if args.has("cycles"):
		await _viewport_cycles(int(args["cycles"]))
	var warmup := int(args.get("frames", "60"))
	for i in warmup:
		await _next_frame()

	if args.has("bench"):
		await _bench(int(args["bench"]))
	if args.has("shot"):
		_save(args["shot"])
	if args.has("sequence"):
		var out: String = args.get("out", ".")
		DirAccess.make_dir_recursive_absolute(out)
		var count := int(args["sequence"])
		var reference := int(args.get("reference", "0"))
		for i in count:
			if reference > 0:
				frozen = true
				# Leaving and re-entering the reference mode restarts its average.
				_set_rr_debug(0)
				await RenderingServer.frame_post_draw
				_set_rr_debug(8)
				for k in reference:
					await RenderingServer.frame_post_draw
				_set_rr_debug(int(args.get("rr_debug", "0")))
				frozen = false
			_save(out.path_join("%s_%03d.png" % [view, i]))
			await _next_frame()
	get_tree().quit()


func _next_frame() -> void:
	await RenderingServer.frame_post_draw
	if args.has("resize_at"):
		var p: PackedStringArray = String(args["resize_at"]).split(":")
		if frame == int(p[0]):
			var r: PackedStringArray = p[1].split("x")
			vp.size = Vector2i(int(r[0]), int(r[1]))
	if args.has("scale_at"):
		var p: PackedStringArray = String(args["scale_at"]).split(":")
		if frame == int(p[0]):
			vp.scaling_3d_mode = int(p[1])
			vp.scaling_3d_scale = float(args.get("scale", "0.67"))


func _save(p_path: String) -> void:
	var err := vp.get_texture().get_image().save_png(p_path)
	if err != OK:
		push_error("Can't save %s: %s" % [p_path, error_string(err)])


func _viewport_cycles(p_count: int) -> void:
	for i in p_count:
		var extra := SubViewport.new()
		extra.size = Vector2i(320 + 16 * i, 180 + 9 * i)
		extra.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var cam := Camera3D.new()
		extra.add_child(cam)
		extra.world_3d = vp.find_world_3d()
		add_child(extra)
		cam.look_at_from_position(camera.global_position, camera.global_position + Vector3(0, 0, -1))
		for k in 10:
			await RenderingServer.frame_post_draw
		extra.queue_free()
		await RenderingServer.frame_post_draw
	print("Viewport cycles: %d" % p_count)


func _bench(p_frames: int) -> void:
	var gpu := 0.0
	var passes := {}
	for i in p_frames:
		await _next_frame()
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp.get_viewport_rid())
		var rd := RenderingServer.get_rendering_device()
		for j in rd.get_captured_timestamps_count() - 1:
			var name := rd.get_captured_timestamp_name(j)
			var key := ""
			if name == "Pathtracer":
				key = "pt"
			elif name.begins_with("RR ") and name != "RR Done":
				key = "rr_" + name.trim_prefix("RR ").to_snake_case()
			if key != "":
				passes[key] = passes.get(key, 0.0) + (rd.get_captured_timestamp_gpu_time(j + 1) - rd.get_captured_timestamp_gpu_time(j)) / 1000000.0
	var text := ""
	var rr_total := 0.0
	for k in passes:
		text += "  %s=%.3f" % [k, passes[k] / p_frames]
		if String(k).begins_with("rr_"):
			rr_total += passes[k] / p_frames
	var vram := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0
	print("BENCH view=%s res=%dx%d spp=%d denoiser=%d frames=%d  gpu_ms=%.3f rr_ms=%.3f%s  vram_mb=%.1f" % [
		view, vp.size.x, vp.size.y, env.pathtracing_samples_per_pixel, env.pathtracing_denoiser, p_frames, gpu / p_frames, rr_total, text, vram])


# --- Scenes --------------------------------------------------------------------

func _build_environment(p_root: Node3D) -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	p_root.add_child(world_env)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, -35, 0)
	sun.light_energy = 1.4
	sun.shadow_enabled = true
	p_root.add_child(sun)


func _build_cornell(a: Node3D) -> void:
	var white := _mat(Color(0.8, 0.8, 0.8))
	_room(a, Vector3(4, 3, 4), white, _mat(Color(0.75, 0.1, 0.1)), _mat(Color(0.1, 0.6, 0.15)), true)
	_box(a, Vector3(0, 2.97, 0), Vector3(1.0, 0.04, 1.0), _emissive(Color(1, 0.95, 0.85), 8.0))
	_box(a, Vector3(-0.7, 0.6, -0.5), Vector3(1.0, 1.2, 1.0), white).rotation.y = 0.3
	_box(a, Vector3(0.7, 0.35, 0.5), Vector3(0.7, 0.7, 0.7), white).rotation.y = -0.4


func _build_pan(a: Node3D) -> void:
	_floor(a, 30.0, _mat(Color(0.5, 0.5, 0.5)))
	_box(a, Vector3(0, 2.0, -3.0), Vector3(20, 4, 0.3), _mat(Color(0.7, 0.65, 0.6)))
	for i in 12:
		var x := -8.25 + i * 1.5
		_box(a, Vector3(x, 1.25, 0), Vector3(0.3, 2.5, 0.3), _mat(Color.from_hsv(i / 12.0, 0.5, 0.8)))
	# A railing of thin bars in front of the pillars.
	for i in 80:
		_box(a, Vector3(-8.0 + i * 0.2, 0.5, 2.0), Vector3(0.025, 1.0, 0.025), _mat(Color(0.2, 0.2, 0.22), 0.4, 1.0))
	_box(a, Vector3(0, 1.0, 2.0), Vector3(16.0, 0.05, 0.05), _mat(Color(0.2, 0.2, 0.22), 0.4, 1.0))


func _build_emissive(a: Node3D) -> void:
	_room(a, Vector3(8, 3, 8), _mat(Color(0.7, 0.7, 0.7)), _mat(Color(0.7, 0.7, 0.7)), _mat(Color(0.7, 0.7, 0.7)))
	# Both stay clear of the ball's path (an ellipse of 2 x 1.5 m): inside them
	# it would be hidden and the room dark.
	_box(a, Vector3(-3.0, 0.5, -2.8), Vector3(1, 1, 1), _mat(Color(0.6, 0.6, 0.65), 0.3))
	_sphere(a, Vector3(2.8, 0.5, -2.8), 0.5, _mat(Color(0.8, 0.6, 0.3), 0.25, 1.0))
	var ball := _sphere(a, Vector3.ZERO, 0.25, _emissive(Color(1.0, 0.6, 0.2), 30.0))
	animated.append(func(t: float) -> void:
		ball.position = Vector3(sin(t * 2.0) * 2.0, 0.6 + 0.3 * sin(t * 3.0), cos(t * 2.0) * 1.5))


func _build_skinned(a: Node3D) -> void:
	_floor(a, 30.0, _mat(Color(0.55, 0.55, 0.5)))
	for i in 3:
		var skel := _tentacle(a, Vector3(-2.0 + i * 2.0, 0, 0), _mat(Color.from_hsv(0.1 + i * 0.3, 0.6, 0.8), 0.5))
		var phase := i * 1.3
		animated.append(func(t: float) -> void:
			skel.set_bone_pose_rotation(1, Quaternion(Vector3(0, 0, 1), sin(t * 2.0 + phase) * 0.7))
			skel.set_bone_pose_rotation(2, Quaternion(Vector3(1, 0, 0), sin(t * 2.6 + phase) * 0.8)))


func _build_thin(a: Node3D) -> void:
	_floor(a, 30.0, _mat(Color(0.5, 0.5, 0.5)))
	_box(a, Vector3(0, 2.0, -4.0), Vector3(16, 4, 0.3), _mat(Color(0.8, 0.8, 0.75)))
	for i in 120:
		_box(a, Vector3(-6.0 + i * 0.1, 0.8, 0.0), Vector3(0.015, 1.6, 0.015), _mat(Color(0.15, 0.15, 0.15), 0.5))
	var leaves := _leaf_material()
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 40:
		var q := MeshInstance3D.new()
		var mesh := QuadMesh.new()
		mesh.size = Vector2(0.8, 0.8)
		q.mesh = mesh
		q.material_override = leaves
		q.position = Vector3(rng.randf_range(-6, 6), rng.randf_range(0.3, 3.0), rng.randf_range(-3.5, -1.0))
		q.rotation = Vector3(rng.randf_range(-0.5, 0.5), rng.randf_range(-1, 1), 0)
		a.add_child(q)


func _build_mirror(a: Node3D) -> void:
	_floor(a, 30.0, _mat(Color(0.5, 0.5, 0.5)))
	_box(a, Vector3(-2.0, 0.01, 0), Vector3(4, 0.02, 6), _mat(Color(0.95, 0.95, 0.95), 0.0, 1.0))
	_box(a, Vector3(2.0, 0.01, 0), Vector3(4, 0.02, 6), _mat(Color(0.1, 0.1, 0.12), 0.15))
	for i in 3:
		_sphere(a, Vector3(-2.0 + i * 2.0, 0.5, -2.5), 0.5, _mat(Color(0.9, 0.7, 0.5), 0.2 + i * 0.2, 1.0))
	_box(a, Vector3(-1.0, 0.5, 1.5), Vector3(1, 1, 1), _mat(Color(0.8, 0.2, 0.2)))
	_box(a, Vector3(2.5, 0.75, 1.0), Vector3(0.5, 1.5, 0.5), _mat(Color(0.2, 0.4, 0.9)))
	var mover := _box(a, Vector3.ZERO, Vector3(0.6, 0.6, 0.6), _mat(Color(0.9, 0.9, 0.2)))
	animated.append(func(t: float) -> void:
		mover.position = Vector3(sin(t * 1.5) * 2.5, 0.8 + 0.5 * sin(t * 2.3), 0.5))


func _build_lights(a: Node3D) -> void:
	_floor(a, 30.0, _mat(Color(0.6, 0.6, 0.6)))
	for x in 5:
		for z in 3:
			_box(a, Vector3(-4.0 + x * 2.0, 0.75, -2.0 + z * 2.0), Vector3(0.3, 1.5, 0.3), _mat(Color(0.8, 0.8, 0.8)))
	var colors := [Color(1, 0.3, 0.2), Color(0.2, 0.5, 1)]
	for i in 2:
		var light := OmniLight3D.new()
		light.light_color = colors[i]
		light.light_energy = 8.0
		light.omni_range = 8.0
		light.shadow_enabled = true
		a.add_child(light)
		var phase := i * PI
		animated.append(func(t: float) -> void:
			light.position = Vector3(sin(t * 1.3 + phase) * 4.0, 1.2, cos(t * 1.3 + phase) * 2.5))
	var spot := SpotLight3D.new()
	spot.light_energy = 15.0
	spot.spot_range = 15.0
	spot.spot_angle = 25.0
	spot.shadow_enabled = true
	spot.position = Vector3(0, 6, 4)
	a.add_child(spot)
	animated.append(func(t: float) -> void:
		spot.look_at_from_position(a.global_position + Vector3(0, 6, 4), a.global_position + Vector3(sin(t * 0.8) * 4.0, 0, -1)))


func _build_dark(a: Node3D) -> void:
	_room(a, Vector3(6, 3, 6), _mat(Color(0.5, 0.5, 0.5)), _mat(Color(0.5, 0.5, 0.5)), _mat(Color(0.5, 0.5, 0.5)))
	var light := OmniLight3D.new()
	light.position = Vector3(-2.2, 2.4, -2.2)
	light.light_energy = 4.0
	light.omni_range = 7.0
	light.light_size = 0.05
	light.shadow_enabled = true
	a.add_child(light)
	_box(a, Vector3(0, 0.6, -1.0), Vector3(1.2, 1.2, 1.2), _mat(Color(0.7, 0.7, 0.7)))
	for i in 6:
		_sphere(a, Vector3(-2.5 + i, 0.05, 2.0), 0.03, _emissive(Color(0.3, 1.0, 0.4), 40.0))


# --- Building blocks -----------------------------------------------------------

func _mat(p_albedo: Color, p_roughness := 0.8, p_metallic := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_albedo
	m.roughness = p_roughness
	m.metallic = p_metallic
	return m


func _emissive(p_color: Color, p_energy: float) -> StandardMaterial3D:
	var m := _mat(p_color, 1.0)
	m.emission_enabled = true
	m.emission = p_color
	m.emission_energy_multiplier = p_energy
	return m


func _leaf_material() -> StandardMaterial3D:
	# Noise blobs in the alpha channel, cut out with alpha scissor.
	var noise := FastNoiseLite.new()
	noise.frequency = 0.08
	var img := Image.create(128, 128, false, Image.FORMAT_RGBA8)
	for y in 128:
		for x in 128:
			var n := noise.get_noise_2d(x, y)
			var edge := 1.0 - Vector2(x - 64, y - 64).length() / 64.0
			img.set_pixel(x, y, Color(0.25, 0.5 + 0.2 * n, 0.15, 1.0 if n + edge * 0.6 > 0.25 else 0.0))
	var m := _mat(Color.WHITE, 0.7)
	m.albedo_texture = ImageTexture.create_from_image(img)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	m.alpha_scissor_threshold = 0.5
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _box(p_parent: Node3D, p_pos: Vector3, p_size: Vector3, p_mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = p_size
	mi.mesh = mesh
	mi.material_override = p_mat
	mi.position = p_pos
	p_parent.add_child(mi)
	return mi


func _sphere(p_parent: Node3D, p_pos: Vector3, p_radius: float, p_mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var mesh := SphereMesh.new()
	mesh.radius = p_radius
	mesh.height = p_radius * 2.0
	mi.mesh = mesh
	mi.material_override = p_mat
	mi.position = p_pos
	p_parent.add_child(mi)
	return mi


func _floor(p_parent: Node3D, p_size: float, p_mat: Material) -> void:
	_box(p_parent, Vector3(0, -0.05, 0), Vector3(p_size, 0.1, p_size), p_mat)


# A room of boxes (walls have a thickness); closed unless p_open_front.
func _room(p_parent: Node3D, p_size: Vector3, p_walls: Material, p_left: Material, p_right: Material, p_open_front := false) -> void:
	var t := 0.1
	_box(p_parent, Vector3(0, -t * 0.5, 0), Vector3(p_size.x, t, p_size.z), p_walls)
	_box(p_parent, Vector3(0, p_size.y + t * 0.5, 0), Vector3(p_size.x, t, p_size.z), p_walls)
	_box(p_parent, Vector3(0, p_size.y * 0.5, -p_size.z * 0.5 - t * 0.5), Vector3(p_size.x, p_size.y, t), p_walls)
	if not p_open_front:
		_box(p_parent, Vector3(0, p_size.y * 0.5, p_size.z * 0.5 + t * 0.5), Vector3(p_size.x, p_size.y, t), p_walls)
	_box(p_parent, Vector3(-p_size.x * 0.5 - t * 0.5, p_size.y * 0.5, 0), Vector3(t, p_size.y, p_size.z), p_left)
	_box(p_parent, Vector3(p_size.x * 0.5 + t * 0.5, p_size.y * 0.5, 0), Vector3(t, p_size.y, p_size.z), p_right)


# A 3 m skinned cylinder with three bones (base, middle, tip).
func _tentacle(p_parent: Node3D, p_pos: Vector3, p_mat: Material) -> Skeleton3D:
	var skel := Skeleton3D.new()
	skel.position = p_pos
	p_parent.add_child(skel)
	for b in 3:
		skel.add_bone("b%d" % b)
		skel.set_bone_rest(b, Transform3D(Basis(), Vector3(0, 0.0 if b == 0 else 1.0, 0)))
		if b > 0:
			skel.set_bone_parent(b, b - 1)
	skel.reset_bone_poses()

	const RINGS := 30
	const SEGMENTS := 24
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	for r in RINGS + 1:
		var y := 3.0 * r / RINGS
		var radius := 0.22 - 0.05 * y
		var b0 := mini(int(y), 2)
		var b1 := mini(b0 + 1, 2)
		var f := clampf(y - b0, 0.0, 1.0)
		for s in SEGMENTS:
			var ang := TAU * s / SEGMENTS
			normals.append(Vector3(cos(ang), 0, sin(ang)))
			verts.append(Vector3(cos(ang) * radius, y, sin(ang) * radius))
			bones.append_array([b0, b1, 0, 0])
			weights.append_array([1.0 - f, f, 0.0, 0.0])
	for r in RINGS:
		for s in SEGMENTS:
			var i0 := r * SEGMENTS + s
			var i1 := r * SEGMENTS + (s + 1) % SEGMENTS
			var i2 := i0 + SEGMENTS
			var i3 := i1 + SEGMENTS
			indices.append_array([i0, i2, i1, i1, i2, i3])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_BONES] = bones
	arrays[Mesh.ARRAY_WEIGHTS] = weights
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = p_mat
	skel.add_child(mi)
	mi.skeleton = NodePath("..")
	var skin := Skin.new()
	for b in 3:
		skin.add_bind(b, skel.get_bone_global_rest(b).affine_inverse())
	mi.skin = skin
	return skel
