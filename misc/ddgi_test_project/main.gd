extends Node3D
## DDGI test scenes. Builds everything from code so the scenes stay easy to
## diff. See DDGI.md in the repository root.
##
## Views:
##   room     closed room: white walls, red and green side walls, colored
##            emissive panels, a moving light and moving objects
##   outdoor  sun with a time-of-day cycle, a large area of repeated
##            buildings and pillars; the camera flies through it (scrolling)
##   stress   thousands of instances, many fast-changing lights, moving
##            geometry and an orbiting camera
##   interior closed house lit only by the sun through one window, with a
##            windowless back room behind a doorway (bounce light only)

const VIEWS := {
	"room": [Vector3(0, 2.0, 3.6), Vector3(0, 1.6, -2.0)],
	"outdoor": [Vector3(200, 6, 40), Vector3(200, 2, 0)],
	"stress": [Vector3(-200, 18, 40), Vector3(-200, 0, 0)],
	"interior": [Vector3(395.0, 1.6, -4.0), Vector3(401.5, 1.0, 0.5)],
}
const VIEW_KEYS := ["room", "outdoor", "stress", "interior"]
const GI_MODES := ["none", "sdfgi", "ddgi"]
const DEBUG_MODE_COUNT := 7

var args := {}
var env: Environment
var sun: DirectionalLight3D
var camera: Camera3D
var hud: Label
var view := "room"
var gi_mode := "ddgi"
var animate := true
var move_camera := false
var time := 0.0
var animated: Array[Callable] = []
var bench_samples := []
# With --volume: a DDGIVolume node (fixed box) instead of the camera-following
# Environment cascades. Needed for baking.
var volume: DDGIVolume
# The viewport the 3D scene renders to: the window, or with --res an
# offscreen SubViewport of exactly that size (the window can't be larger
# than the screen).
var vp: Viewport


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts: PackedStringArray = arg.trim_prefix("--").split("=", true, 1)
		args[parts[0]] = parts[1] if parts.size() > 1 else "1"

	vp = get_viewport()
	if args.has("res"):
		var res: PackedStringArray = args["res"].split("x")
		var sub := SubViewport.new()
		sub.size = Vector2i(int(res[0]), int(res[1]))
		sub.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		add_child(sub)
		vp = sub

	_build_environment()
	_build_room(Vector3(0, 0, 0))
	_build_outdoor(Vector3(200, 0, 0))
	_build_stress(Vector3(-200, 0, 0))
	_build_interior(Vector3(400, 0, 0))

	camera = Camera3D.new()
	camera.fov = 60.0
	camera.far = 600.0
	if vp == get_viewport():
		add_child(camera)
	else:
		vp.add_child(camera)
		# Show the offscreen image in the window too.
		var rect := TextureRect.new()
		rect.texture = (vp as SubViewport).get_texture()
		rect.set_anchors_preset(Control.PRESET_FULL_RECT)
		rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		add_child(rect)

	hud = Label.new()
	hud.position = Vector2(12, 8)
	hud.add_theme_color_override("font_outline_color", Color.BLACK)
	hud.add_theme_constant_override("outline_size", 6)
	var hud_layer := CanvasLayer.new()
	hud_layer.add_child(hud)
	add_child(hud_layer)

	_apply_args()
	_setup_volume()

	if args.has("shot") or args.has("bench") or args.has("measure") or args.has("weather"):
		hud.visible = false
		_run_capture()


func _apply_args() -> void:
	animate = args.get("anim", "1") == "1"
	move_camera = args.get("move", "0") == "1"
	if args.has("quality"):
		ProjectSettings.set_setting("rendering/global_illumination/ddgi/quality", int(args["quality"]))
	if args.has("budget"):
		ProjectSettings.set_setting("rendering/global_illumination/ddgi/gpu_time_budget_ms", float(args["budget"]))
	if args.has("half"):
		RenderingServer.gi_set_use_half_resolution(args["half"] == "1")
	if args.has("cascades"):
		env.ddgi_cascades = int(args["cascades"])
	if args.has("spacing"):
		env.ddgi_probe_spacing = float(args["spacing"])
	if args.has("grid"):
		var g: PackedStringArray = args["grid"].split(",")
		env.ddgi_probe_grid = Vector3i(int(g[0]), int(g[1]), int(g[2]))
	if args.has("hysteresis"):
		env.ddgi_hysteresis = float(args["hysteresis"])
	if args.has("energy"):
		env.ddgi_energy = float(args["energy"])
	if args.has("realtime"):
		env.ddgi_realtime_updates = args["realtime"] == "1"
	if args.has("rt_rays"):
		ProjectSettings.set_setting("rendering/global_illumination/ddgi/realtime_rays_per_probe", int(args["rt_rays"]))
	if args.has("reloc"):
		env.ddgi_probe_relocation = args["reloc"] == "1"
	if args.has("classify"):
		env.ddgi_probe_classification = args["classify"] == "1"
	env.ddgi_debug_mode = int(args.get("debug", "0"))
	env.pathtracing_enabled = args.get("pt", "0") == "1"
	env.pathtracing_samples_per_pixel = int(args.get("spp", "2"))
	if args.has("bounces"):
		env.pathtracing_max_bounces = int(args["bounces"])
	env.pathtracing_denoiser = int(args.get("denoiser", "0"))
	if args.has("scale3d"):
		# 0 bilinear, 1 FSR1, 2 FSR2, 6 DLSS (see Viewport.Scaling3DMode).
		vp.scaling_3d_mode = int(args["scale3d"])
		vp.scaling_3d_scale = float(args.get("scale", "0.67"))
	if args.has("linear"):
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		env.tonemap_exposure = float(args.get("exposure", "1.0"))
	_set_gi_mode(args.get("gi", "ddgi"))
	_set_view(args.get("view", "room"))


func _set_gi_mode(p_mode: String) -> void:
	gi_mode = p_mode
	env.ddgi_enabled = p_mode == "ddgi"
	env.sdfgi_enabled = p_mode == "sdfgi"
	if hud:
		_update_hud()


func _set_view(p_view: String) -> void:
	if not VIEWS.has(p_view):
		push_error("Unknown view '%s'. Known: %s" % [p_view, ", ".join(VIEWS.keys())])
		p_view = "room"
	view = p_view
	camera.look_at_from_position(VIEWS[view][0], VIEWS[view][1])
	# The sun is for the outdoor and stress areas; the room is closed anyway.
	_update_hud()


func _update_hud() -> void:
	var q: int = ProjectSettings.get_setting("rendering/global_illumination/ddgi/quality")
	hud.text = "view: %s   GI: %s   DDGI quality: %s   debug: %d   animation: %s   path tracing: %s\n1-4 views   G GI mode   U quality   Tab debug   L animation   M camera path   P path tracing\nfly: hold right mouse to look, WASD move, Q/E down/up, Shift faster" % [
		view, gi_mode, ["Low", "Medium", "High", "Ultra", "Custom"][q], env.ddgi_debug_mode,
		"on" if animate else "off", "on" if env.pathtracing_enabled else "off"]


func _process(delta: float) -> void:
	# Fixed time step when capturing, so a given --frames is reproducible.
	var capturing := args.has("shot") or args.has("bench") or args.has("measure") or args.has("weather")
	var dt := 1.0 / 60.0 if capturing else delta
	if animate:
		time += dt
		for f in animated:
			f.call(time)
	if move_camera:
		_camera_path(dt)
	elif not capturing:
		_fly(delta)


# Camera paths for moving-camera tests: through the outdoor area (scrolling
# probe volumes) or around the stress test.
func _camera_path(dt: float) -> void:
	if view == "outdoor":
		var speed := float(args.get("speed", "8.0"))
		camera.position.z -= speed * dt
		if camera.position.z < -160.0:
			camera.position.z = 40.0
	else:
		var target: Vector3 = VIEWS[view][1]
		var offset := camera.position - target
		camera.look_at_from_position(target + offset.rotated(Vector3.UP, 0.4 * dt), target)


const FLY_SPEED := 6.0
const FLY_SENSITIVITY := 0.003


func _fly(delta: float) -> void:
	var move := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		move.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		move.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		move.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		move.x += 1.0
	if Input.is_physical_key_pressed(KEY_E):
		move.y += 1.0
	if Input.is_physical_key_pressed(KEY_Q):
		move.y -= 1.0
	if move != Vector3.ZERO:
		var speed := FLY_SPEED * (5.0 if Input.is_physical_key_pressed(KEY_SHIFT) else 1.0)
		camera.translate(move.normalized() * speed * delta)


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button and button.button_index == MOUSE_BUTTON_RIGHT:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if button.pressed else Input.MOUSE_MODE_VISIBLE
	var motion := event as InputEventMouseMotion
	if motion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var rot := camera.rotation
		rot.y -= motion.relative.x * FLY_SENSITIVITY
		rot.x = clampf(rot.x - motion.relative.y * FLY_SENSITIVITY, -1.5, 1.5)
		rot.z = 0.0
		camera.rotation = rot


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode >= KEY_1 and key.keycode <= KEY_4:
		_set_view(VIEW_KEYS[key.keycode - KEY_1])
	elif key.keycode == KEY_G:
		_set_gi_mode(GI_MODES[(GI_MODES.find(gi_mode) + 1) % GI_MODES.size()])
	elif key.keycode == KEY_U:
		var q: int = ProjectSettings.get_setting("rendering/global_illumination/ddgi/quality")
		ProjectSettings.set_setting("rendering/global_illumination/ddgi/quality", (q + 1) % 4)
	elif key.keycode == KEY_TAB:
		env.ddgi_debug_mode = (env.ddgi_debug_mode + 1) % DEBUG_MODE_COUNT
	elif key.keycode == KEY_L:
		animate = not animate
	elif key.keycode == KEY_M:
		move_camera = not move_camera
	elif key.keycode == KEY_P:
		env.pathtracing_enabled = not env.pathtracing_enabled
	_update_hud()


# --volume=cx,cy,cz,sx,sy,sz adds a DDGIVolume (center and size) with
# --vspacing= and --vcascades= (default 1 m, 1 cascade). --bake=path bakes it
# before the capture and saves the DDGIProbeData; --baked=path loads one with
# --bake_mode=0|1|2 (dynamic, baked, baked + dynamic; default 1).
func _setup_volume() -> void:
	if not args.has("volume"):
		return
	var v: PackedStringArray = args["volume"].split(",")
	volume = DDGIVolume.new()
	volume.position = Vector3(float(v[0]), float(v[1]), float(v[2]))
	volume.size = Vector3(float(v[3]), float(v[4]), float(v[5]))
	volume.probe_spacing = float(args.get("vspacing", "1.0"))
	volume.cascades = int(args.get("vcascades", "1"))
	volume.enabled = gi_mode == "ddgi"
	if args.has("bounce"):
		volume.bounce_energy = float(args["bounce"])
	volume.realtime_updates = env.ddgi_realtime_updates
	add_child(volume)
	if args.has("baked"):
		volume.probe_data = load(args["baked"])
		volume.bake_mode = int(args.get("bake_mode", "1"))


# --- Screenshots and benchmarks ---------------------------------------------

# --frames=N renders N frames first (probes converge), then either saves
# --shot=path.png, or measures --bench=N more frames and prints one BENCH line:
# GPU and CPU frame time, the DDGI pass times (with --gpu-profile on the
# engine command line) and video memory.
func _run_capture() -> void:
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	if args.has("bake") and volume:
		# --ps_scale3d=N: the project's default 3D scaling mode during the bake
		# (new viewports, like the bake's own, take it).
		if args.has("ps_scale3d"):
			ProjectSettings.set_setting("rendering/scaling_3d/mode", int(args["ps_scale3d"]))
			ProjectSettings.set_setting("rendering/scaling_3d/scale", 0.67)
		await RenderingServer.frame_post_draw
		var t0 := Time.get_ticks_msec()
		var data := volume.bake()
		var bake_ms := Time.get_ticks_msec() - t0
		var err := ResourceSaver.save(data, args["bake"], ResourceSaver.FLAG_COMPRESS) if data else FAILED
		print("BAKE %s in %d ms: %s, %d bytes" % [args["bake"], bake_ms, error_string(err), FileAccess.get_file_as_bytes(args["bake"]).size() if err == OK else 0])
	for i in int(args.get("frames", "120")):
		await RenderingServer.frame_post_draw

	if args.has("bench"):
		var frames := int(args["bench"])
		var gpu := 0.0
		var cpu := 0.0
		var passes := {}
		var start := Time.get_ticks_usec()
		for i in frames:
			await RenderingServer.frame_post_draw
			gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp.get_viewport_rid())
			cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp.get_viewport_rid())
			var p := _ddgi_pass_times()
			for k in p:
				passes[k] = passes.get(k, 0.0) + p[k]
		var wall_ms := (Time.get_ticks_usec() - start) / 1000.0 / frames
		var pass_text := ""
		var ddgi_total := 0.0
		for k in passes:
			pass_text += "  %s=%.3f" % [k, passes[k] / frames]
			ddgi_total += passes[k] / frames
		var vram := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0
		var tex_mem := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED) / 1048576.0
		var buf_mem := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_BUFFER_MEM_USED) / 1048576.0
		print("BENCH view=%s gi=%s quality=%s half=%s budget=%s move=%s res=%dx%d scale3d=%s frames=%d  gpu_ms=%.3f cpu_ms=%.3f frame_ms=%.3f fps=%.1f  ddgi_ms=%.3f%s  vram_mb=%.1f tex_mb=%.1f buf_mb=%.1f" % [
			view, gi_mode, args.get("quality", "1"), args.get("half", "0"), args.get("budget", "0"), args.get("move", "0"), vp.get_visible_rect().size.x, vp.get_visible_rect().size.y,
			args.get("scale3d", "native"), frames, gpu / frames, cpu / frames, wall_ms, 1000.0 / wall_ms,
			ddgi_total, pass_text, vram, tex_mem, buf_mem])

	if args.has("measure"):
		await _measure(int(args["measure"]))

	# --weather=dir: a storm rolls in over --weather_frames (default 120)
	# frames: the sun dims to a cold light and the sky darkens. Every frame
	# from the start to 120 frames after the end is saved as dir/fNNN.png
	# (use --debug=1 to see only the indirect light).
	if args.has("weather"):
		var dir: String = args["weather"]
		DirAccess.make_dir_recursive_absolute(dir)
		var n := int(args.get("weather_frames", "120"))
		var sky_mat := env.sky.sky_material as ProceduralSkyMaterial
		for f in n + 120:
			var t := clampf(float(f) / n, 0.0, 1.0)
			sun.light_energy = lerpf(1.5, 0.25, t)
			sun.light_color = Color.WHITE.lerp(Color(0.6, 0.66, 0.8), t)
			sky_mat.energy_multiplier = lerpf(1.0, 0.25, t)
			await RenderingServer.frame_post_draw
			vp.get_texture().get_image().save_png(dir.path_join("f%03d.png" % f))
		print("WEATHER %d frames saved to %s" % [n + 120, dir])

	if args.has("shot"):
		var path: String = args["shot"]
		var err := vp.get_texture().get_image().save_png(path)
		print("Screenshot %s: %s" % [path, error_string(err)])

	# --settle=N: stop the camera after --shot, wait N frames for the probes to
	# converge and save --shot with _settled appended. The difference between
	# the two images is the error that scrolling leaves while moving.
	if args.has("settle") and args.has("shot"):
		move_camera = false
		for i in int(args["settle"]):
			await RenderingServer.frame_post_draw
		var path: String = String(args["shot"]).get_basename() + "_settled.png"
		vp.get_texture().get_image().save_png(path)
		print("Screenshot %s" % path)

	# --switch: turn every light and emitter of the current view off, then
	# save --shot with _N appended N frames later for each N in --after
	# (default 1,5,10,20,40,80), to measure how fast the GI follows.
	if args.has("switch") and args.has("shot"):
		for light in find_children("*", "Light3D", true, false):
			light.visible = false
		for node in find_children("*", "MeshInstance3D", true, false):
			var mat := (node as MeshInstance3D).material_override as StandardMaterial3D
			if mat and mat.emission_enabled:
				mat.emission_energy_multiplier = 0.0
		# Something to light the room again: one white panel on the ceiling.
		var panel := _box(self, Vector3(0, 3.95, 0), Vector3(1.5, 0.05, 1.5), _emissive(Color.WHITE, 8.0))
		panel.name = "SwitchPanel"
		var frame := 0
		for n in String(args.get("after", "1,5,10,20,40,80")).split(","):
			while frame < int(n):
				await RenderingServer.frame_post_draw
				frame += 1
			var path: String = String(args["shot"]).get_basename() + "_%d.png" % frame
			vp.get_texture().get_image().save_png(path)
			print("Screenshot %s" % path)
	get_tree().quit()


# --measure=N: N frames of the final image (sRGB decoded to linear). Prints
# one MEASURE line: the mean linear luminance (brightness, compare against
# --pt=1), the mean temporal standard deviation of each pixel in 8-bit levels
# (flicker) and the share of pixels whose deviation is above 2 levels. With
# --mean=path the per-pixel mean image is saved too, with --stdmap=path the
# deviation (white = 8 levels). Every second pixel per
# axis is used. With --anim=0 a perfect result has no flicker at all.
func _measure(p_frames: int) -> void:
	var w := 0
	var h := 0
	var sum := PackedFloat32Array()
	var sum_sq := PackedFloat32Array()
	var sum_rgb := PackedFloat32Array()
	for f in p_frames:
		await RenderingServer.frame_post_draw
		var img := vp.get_texture().get_image()
		img.convert(Image.FORMAT_RGB8)
		var data := img.get_data()
		if f == 0:
			w = img.get_width() / 2
			h = img.get_height() / 2
			sum.resize(w * h)
			sum_sq.resize(w * h)
			sum_rgb.resize(w * h * 3)
			sum.fill(0.0)
			sum_sq.fill(0.0)
			sum_rgb.fill(0.0)
		var stride := img.get_width() * 3
		if args.has("trace_px"):
			var tp: PackedStringArray = args["trace_px"].split(",")
			var tc := img.get_pixel(int(tp[0]), int(tp[1]))
			print("PX %d %.1f" % [f, 255.0 * (0.2126 * tc.r + 0.7152 * tc.g + 0.0722 * tc.b)])
		for y in h:
			var row := y * 2 * stride
			for x in w:
				var o := row + x * 6
				var r := data[o]
				var g := data[o + 1]
				var b := data[o + 2]
				var luma := 0.2126 * r + 0.7152 * g + 0.0722 * b
				var i := y * w + x
				sum[i] += luma
				sum_sq[i] += luma * luma
				sum_rgb[i * 3] += r
				sum_rgb[i * 3 + 1] += g
				sum_rgb[i * 3 + 2] += b
	var n := float(p_frames)
	var lin_sum := 0.0
	var std_sum := 0.0
	var flicker_pixels := 0
	var mean_img := Image.create(w, h, false, Image.FORMAT_RGB8)
	var std_img := Image.create(w, h, false, Image.FORMAT_RGB8)
	for y in h:
		for x in w:
			var i := y * w + x
			var c := Color8(int(sum_rgb[i * 3] / n), int(sum_rgb[i * 3 + 1] / n), int(sum_rgb[i * 3 + 2] / n))
			mean_img.set_pixel(x, y, c)
			var lc := c.srgb_to_linear()
			lin_sum += 0.2126 * lc.r + 0.7152 * lc.g + 0.0722 * lc.b
			var m := sum[i] / n
			var sd := sqrt(maxf(sum_sq[i] / n - m * m, 0.0))
			std_sum += sd
			std_img.set_pixel(x, y, Color(minf(sd / 8.0, 1.0), minf(sd / 8.0, 1.0), minf(sd / 8.0, 1.0)))
			if sd > 2.0:
				flicker_pixels += 1
	var count := float(w * h)
	print("MEASURE view=%s gi=%s quality=%s anim=%s frames=%d  mean_linear=%.4f  flicker_std=%.3f  flicker_px=%.2f%%" % [
		view, gi_mode, args.get("quality", "1"), args.get("anim", "1"), p_frames, lin_sum / count, std_sum / count, 100.0 * flicker_pixels / count])
	if args.has("mean"):
		mean_img.save_png(args["mean"])
	if args.has("stdmap"):
		# White = 8 levels or more of standard deviation.
		std_img.save_png(args["stdmap"])


# DDGI pass times (ms) of the last captured frame, from the RenderingDevice
# timestamps (needs --gpu-profile). Each pass lasts until the next timestamp.
func _ddgi_pass_times() -> Dictionary:
	var rd := RenderingServer.get_rendering_device()
	var result := {}
	if rd == null:
		return result
	var count := rd.get_captured_timestamps_count()
	var in_as := false
	for i in count - 1:
		var name := rd.get_captured_timestamp_name(i)
		# GPU times are in nanoseconds.
		var ms := (rd.get_captured_timestamp_gpu_time(i + 1) - rd.get_captured_timestamp_gpu_time(i)) / 1000000.0
		var key := ""
		if name == "DDGI Build Acceleration Structures":
			in_as = true
			key = "as"
		elif in_as and (name == "BLAS Build" or name == "TLAS Build"):
			key = "as"
		elif name == "DDGI Done":
			in_as = false
		elif name.begins_with("DDGI "):
			in_as = false
			key = name.trim_prefix("DDGI ").to_snake_case()
		else:
			in_as = false
		if key != "":
			result[key] = result.get(key, 0.0) + ms
	return result


# --- Building blocks --------------------------------------------------------

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


func _box(p_parent: Node3D, p_pos: Vector3, p_size: Vector3, p_mat: Material, p_name := "") -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = p_size
	mi.mesh = mesh
	mi.material_override = p_mat
	mi.position = p_pos
	if p_name != "":
		mi.name = p_name
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


func _build_environment() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sky_mat := ProceduralSkyMaterial.new()
	sky.sky_material = sky_mat
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -30, 0)
	sun.light_energy = 1.5
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 200.0
	add_child(sun)
	animated.append(func(t: float) -> void:
		if args.get("tod", "1") == "1":
			# A day in 60 seconds: the sun sweeps from morning to evening.
			sun.rotation_degrees = Vector3(-15.0 - 60.0 * absf(sin(t * TAU / 60.0)), -30.0 + t * 6.0, 0))


# Closed room (walls are boxes, so probes outside them are classified as
# outside). Lit by a moving omni light and emissive panels only.
func _build_room(o: Vector3) -> void:
	var room := Node3D.new()
	room.name = "Room"
	room.position = o
	add_child(room)
	var white := _mat(Color(0.8, 0.8, 0.8))
	var t := 0.3
	_box(room, Vector3(0, -t * 0.5, 0), Vector3(8 + 2 * t, t, 8 + 2 * t), white) # Floor.
	_box(room, Vector3(0, 4 + t * 0.5, 0), Vector3(8 + 2 * t, t, 8 + 2 * t), white) # Ceiling.
	_box(room, Vector3(0, 2, -4 - t * 0.5), Vector3(8, 4, t), white) # Back.
	_box(room, Vector3(0, 2, 4 + t * 0.5), Vector3(8, 4, t), white) # Front (behind the camera).
	_box(room, Vector3(-4 - t * 0.5, 2, 0), Vector3(t, 4, 8), _mat(Color(0.75, 0.08, 0.06))) # Left, red.
	_box(room, Vector3(4 + t * 0.5, 2, 0), Vector3(t, 4, 8), _mat(Color(0.08, 0.6, 0.1))) # Right, green.

	# Colored emissive panels: they light the room only through GI.
	_box(room, Vector3(-1.5, 2.6, -3.95), Vector3(1.6, 0.6, 0.05), _emissive(Color(1.0, 0.45, 0.1), 6.0), "PanelOrange")
	_box(room, Vector3(1.5, 2.6, -3.95), Vector3(1.6, 0.6, 0.05), _emissive(Color(0.1, 0.5, 1.0), 6.0), "PanelBlue")

	# Static furniture.
	_box(room, Vector3(-2.0, 0.6, -1.5), Vector3(1.2, 1.2, 1.2), white)
	_box(room, Vector3(2.2, 1.0, -2.2), Vector3(0.9, 2.0, 0.9), _mat(Color(0.9, 0.85, 0.3)))

	# Moving objects.
	var spinner := _box(room, Vector3(0.5, 0.8, -0.5), Vector3(1.0, 1.0, 1.0), _mat(Color(0.9, 0.9, 0.9)))
	var ball := _sphere(room, Vector3(0, 0.5, 1.0), 0.5, _mat(Color(0.2, 0.3, 0.9)))
	animated.append(func(time_s: float) -> void:
		spinner.rotation.y = time_s * 0.8
		ball.position = Vector3(sin(time_s * 0.7) * 2.5, 0.5 + absf(sin(time_s * 2.0)) * 1.0, 1.0))

	# A moving, color-changing omni light.
	var lamp := OmniLight3D.new()
	lamp.omni_range = 9.0
	lamp.light_energy = 2.0
	lamp.shadow_enabled = true
	lamp.position = Vector3(0, 3.2, 0)
	room.add_child(lamp)
	animated.append(func(time_s: float) -> void:
		lamp.position = Vector3(sin(time_s * 0.5) * 2.5, 3.2, cos(time_s * 0.5) * 2.0 - 0.5)
		lamp.light_color = Color.from_hsv(fmod(time_s * 0.05, 1.0), 0.3, 1.0))


# Outdoor area: rows of open-fronted buildings, colored walls and pillars over
# a 400 m ground, so the camera can fly far through scrolling probe volumes.
func _build_outdoor(o: Vector3) -> void:
	var area := Node3D.new()
	area.name = "Outdoor"
	area.position = o
	add_child(area)
	_box(area, Vector3(0, -0.5, -60), Vector3(240, 1, 400), _mat(Color(0.45, 0.42, 0.38)))
	var colors := [Color(0.8, 0.2, 0.15), Color(0.2, 0.6, 0.25), Color(0.85, 0.8, 0.7), Color(0.25, 0.35, 0.8), Color(0.9, 0.7, 0.2)]
	var rng := RandomNumberGenerator.new()
	rng.seed = 1234
	for row in range(-12, 4):
		for side in [-1, 1]:
			var z := row * 14.0
			var x: float = side * (10.0 + rng.randf() * 6.0)
			var h := 4.0 + rng.randf() * 8.0
			var w := 6.0 + rng.randf() * 4.0
			var c: Color = colors[rng.randi() % colors.size()]
			# Three walls and a roof: an open-fronted building facing the street.
			var b := Node3D.new()
			b.position = Vector3(x, 0, z)
			area.add_child(b)
			_box(b, Vector3(side * w * 0.5, h * 0.5, 0), Vector3(0.4, h, 8), _mat(c))
			_box(b, Vector3(0, h * 0.5, -4), Vector3(w, h, 0.4), _mat(c))
			_box(b, Vector3(0, h * 0.5, 4), Vector3(w, h, 0.4), _mat(c))
			_box(b, Vector3(0, h + 0.2, 0), Vector3(w + 0.4, 0.4, 8.4), _mat(Color(0.3, 0.3, 0.3)))
		# Pillars along the street.
		for k in 3:
			_box(area, Vector3(-3 + k * 3.0, 2.0, row * 14.0 + 7.0), Vector3(0.6, 4.0, 0.6), _mat(Color(0.85, 0.85, 0.85)))


# Stress test: thousands of instances (MultiMesh), moving geometry and many
# fast-changing lights.
func _build_stress(o: Vector3) -> void:
	var area := Node3D.new()
	area.name = "Stress"
	area.position = o
	add_child(area)
	_box(area, Vector3(0, -0.5, 0), Vector3(120, 1, 120), _mat(Color(0.6, 0.6, 0.6)))

	var count := int(args.get("instances", "4000"))
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	var cube := BoxMesh.new()
	cube.size = Vector3(1, 1, 1)
	var cube_mat := _mat(Color.WHITE)
	cube_mat.vertex_color_use_as_albedo = true
	cube.material = cube_mat
	mm.mesh = cube
	mm.instance_count = count
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	for i in count:
		var p := Vector3(rng.randf_range(-55, 55), 0, rng.randf_range(-55, 55))
		var s := rng.randf_range(0.5, 2.5)
		var h := rng.randf_range(0.5, 6.0)
		mm.set_instance_transform(i, Transform3D(Basis.from_scale(Vector3(s, h, s)).rotated(Vector3.UP, rng.randf() * TAU), p + Vector3(0, h * 0.5, 0)))
		mm.set_instance_color(i, Color.from_hsv(rng.randf(), 0.5, 0.9))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	area.add_child(mmi)

	# Moving geometry.
	var movers: Array[MeshInstance3D] = []
	for i in 40:
		movers.append(_box(area, Vector3(rng.randf_range(-40, 40), 3.0, rng.randf_range(-40, 40)), Vector3(3, 0.5, 3), _mat(Color.from_hsv(rng.randf(), 0.7, 0.9))))
	animated.append(func(t: float) -> void:
		for i in movers.size():
			movers[i].position.y = 3.0 + sin(t * 1.5 + i) * 2.5
			movers[i].rotation.y = t + i)

	# Many lights that change quickly.
	var lights: Array[OmniLight3D] = []
	for i in int(args.get("lights", "32")):
		var l := OmniLight3D.new()
		l.omni_range = 14.0
		l.light_energy = 3.0
		l.position = Vector3(rng.randf_range(-50, 50), 2.5, rng.randf_range(-50, 50))
		area.add_child(l)
		lights.append(l)
	animated.append(func(t: float) -> void:
		for i in lights.size():
			lights[i].light_color = Color.from_hsv(fmod(t * 0.5 + i * 0.13, 1.0), 1.0, 1.0)
			lights[i].light_energy = 1.5 + 1.5 * sin(t * 4.0 + i))


# Interior: a closed house lit only by the sun through one window in the
# front wall. A partition with a doorway separates a windowless back room that
# only gets bounce light. Thick walls, so the probes inside them are found.
func _build_interior(o: Vector3) -> void:
	var house := Node3D.new()
	house.name = "Interior"
	house.position = o
	add_child(house)
	_box(house, Vector3(0, -0.5, 0), Vector3(60, 1, 60), _mat(Color(0.4, 0.45, 0.35))) # Ground outside.
	var plaster := _mat(Color(0.75, 0.72, 0.68))
	var t := 0.3
	var hgt := 3.0
	_box(house, Vector3(0, hgt + t * 0.5, 0), Vector3(12 + 2 * t, t, 10 + 2 * t), plaster) # Ceiling.
	_box(house, Vector3(0, 0.02, 0), Vector3(12, 0.04, 10), _mat(Color(0.55, 0.4, 0.28))) # Wooden floor.
	_box(house, Vector3(0, hgt * 0.5, -5 - t * 0.5), Vector3(12 + 2 * t, hgt, t), plaster) # Back.
	_box(house, Vector3(-6 - t * 0.5, hgt * 0.5, 0), Vector3(t, hgt, 10), plaster) # Left.
	_box(house, Vector3(6 + t * 0.5, hgt * 0.5, 0), Vector3(t, hgt, 10), plaster) # Right.
	# Front wall (toward the sun) with a window from x -3.5 to -0.5, y 0.9 to 2.3.
	var front_z := 5 + t * 0.5
	_box(house, Vector3(-4.75 - t * 0.5, hgt * 0.5, front_z), Vector3(2.5 + t, hgt, t), plaster)
	_box(house, Vector3(2.75 + t * 0.5, hgt * 0.5, front_z), Vector3(6.5 + t, hgt, t), plaster)
	_box(house, Vector3(-2.0, 0.45, front_z), Vector3(3.0, 0.9, t), plaster)
	_box(house, Vector3(-2.0, 2.65, front_z), Vector3(3.0, 0.7, t), plaster)
	# Partition at x = 1.5 with a doorway from z -1.0 to 0.4, 2.2 m high.
	_box(house, Vector3(1.5, hgt * 0.5, -3.0), Vector3(t, hgt, 4.0), plaster)
	_box(house, Vector3(1.5, hgt * 0.5, 2.7), Vector3(t, hgt, 4.6), plaster)
	_box(house, Vector3(1.5, 2.6, -0.3), Vector3(t, 0.8, 1.4), plaster)
	# Furniture: a sofa block and a table in the lit room, a cabinet in the back room.
	_box(house, Vector3(-4.5, 0.4, -2.5), Vector3(1.0, 0.8, 2.5), _mat(Color(0.2, 0.3, 0.6)))
	_box(house, Vector3(-1.5, 0.75, -1.0), Vector3(1.4, 0.06, 0.9), _mat(Color(0.6, 0.45, 0.3)))
	_box(house, Vector3(4.5, 0.9, -3.8), Vector3(1.6, 1.8, 0.6), _mat(Color(0.7, 0.25, 0.2)))
	_sphere(house, Vector3(3.5, 0.4, 1.5), 0.4, _mat(Color(0.85, 0.85, 0.85)))
