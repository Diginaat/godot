extends Node3D
## Path tracer test scene. Builds every test area from code so the scene stays
## easy to diff and extend. See PATHTRACER_TESTING.md in the repository root.
##
## Areas (each has a camera view):
##   pbr       roughness x metallic sphere grid, plus a normal-mapped sphere
##   lighting  Cornell box with omni and spot lights (color bleeding, soft shadows)
##   emissive  dark room lit only by emissive meshes
##   shaders   custom ShaderMaterials (fragment, TIME, normal map, alpha, vertex)
##   glass     alpha-blended and refractive materials
##   fog       volumetric fog, a FogVolume and a spot light shaft
##   lights    closed room lit by 48 small omni lights (many-light sampling)

const VIEWS := {
	"overview": [Vector3(0, 14, 30), Vector3(0, 1, 0)],
	"pbr": [Vector3(0, 3.6, 9.5), Vector3(0, 3.0, 0)],
	"lighting": [Vector3(16, 2.5, 7.5), Vector3(16, 2.0, 0)],
	"emissive": [Vector3(-16, 2.2, 6.5), Vector3(-16, 1.6, 0)],
	"shaders": [Vector3(0, 2.4, -7.5), Vector3(0, 1.2, -14)],
	"glass": [Vector3(16, 2.0, -8.5), Vector3(16, 1.2, -14)],
	"fog": [Vector3(-16, 3.0, -4.0), Vector3(-16, 2.0, -14)],
	"lights": [Vector3(0, 2.2, -21.0), Vector3(0, 1.4, -28)],
}
const VIEW_KEYS := ["pbr", "lighting", "emissive", "shaders", "glass", "fog", "overview", "lights"]
const DEBUG_MODE_COUNT := 23

var args := {}
var env: Environment
var camera: Camera3D
var hud: Label
var view := "overview"


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts: PackedStringArray = arg.trim_prefix("--").split("=", true, 1)
		args[parts[0]] = parts[1] if parts.size() > 1 else "1"

	_build_environment()
	_build_ground()
	_build_pbr(Vector3(0, 0, 0))
	_build_lighting(Vector3(16, 0, 0))
	_build_emissive(Vector3(-16, 0, 0))
	_build_shaders(Vector3(0, 0, -14))
	_build_glass(Vector3(16, 0, -14))
	_build_fog(Vector3(-16, 0, -14))
	_build_many_lights(Vector3(0, 0, -28))

	if args.has("sun_only"):
		# Keep only the sun: isolates direct-light sampling from light selection.
		for light in find_children("*", "Light3D", true, false):
			if not light is DirectionalLight3D:
				light.free()

	camera = Camera3D.new()
	camera.fov = 55.0
	add_child(camera)

	hud = Label.new()
	hud.position = Vector2(12, 8)
	hud.add_theme_color_override("font_outline_color", Color.BLACK)
	hud.add_theme_constant_override("outline_size", 6)
	var hud_layer := CanvasLayer.new()
	hud_layer.add_child(hud)
	add_child(hud_layer)

	_apply_args()

	if args.has("shot"):
		hud.visible = false
		_take_screenshot()
	elif args.has("bench"):
		hud.visible = false
		_benchmark()


func _apply_args() -> void:
	env.pathtracing_enabled = args.get("pt", "1") == "1"
	env.pathtracing_samples_per_pixel = int(args.get("spp", "4"))
	env.pathtracing_max_bounces = int(args.get("bounces", "3"))
	env.pathtracing_debug_mode = int(args.get("debug", "0"))
	env.pathtracing_denoiser = int(args.get("denoiser", "0"))
	if args.has("ser"):
		# Read by the path tracer every frame, so it can change at run time.
		ProjectSettings.set_setting("rendering/pathtracing/use_shader_execution_reordering", args["ser"] == "1")
	env.pathtracing_adaptive_sampling = args.get("adaptive", "0") == "1"
	env.pathtracing_restir_di = args.get("restir", "0") == "1"
	if args.has("adaptive_threshold"):
		ProjectSettings.set_setting("rendering/pathtracing/adaptive_sampling_threshold", float(args["adaptive_threshold"]))
	if args.has("adaptive_debug"):
		ProjectSettings.set_setting("rendering/pathtracing/adaptive_sampling_debug", args["adaptive_debug"] == "1")
	if args.has("vfog_sky_affect"):
		env.volumetric_fog_sky_affect = float(args["vfog_sky_affect"])
	# Measurement mode: linear tonemap and a low exposure keep values from
	# clipping, so mean brightness can be compared between builds.
	if args.has("linear"):
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		env.tonemap_exposure = float(args.get("exposure", "0.25"))
	if args.has("panel_only"):
		# Only the emissive room's ceiling panel emits (a large, easy emitter).
		for node in find_children("*", "MeshInstance3D", true, false):
			var mat := (node as MeshInstance3D).material_override as StandardMaterial3D
			if mat and mat.emission_enabled and node.name != "CeilingPanel":
				mat.emission_energy_multiplier = 0.0
	_set_view(args.get("view", "overview"))


func _set_view(p_view: String) -> void:
	if not VIEWS.has(p_view):
		push_error("Unknown view '%s'. Known: %s" % [p_view, ", ".join(VIEWS.keys())])
		p_view = "overview"
	view = p_view
	camera.look_at_from_position(VIEWS[view][0], VIEWS[view][1])
	if args.has("yaw"):
		# Static camera turned around the view target (see --orbit).
		var target: Vector3 = VIEWS[view][1]
		camera.look_at_from_position(target + (camera.position - target).rotated(Vector3.UP, deg_to_rad(float(args["yaw"]))), target)
	# Volumetric fog only in the fog view, unless forced with --volfog.
	env.volumetric_fog_enabled = args.get("volfog", "1" if view == "fog" else "0") == "1"
	_update_hud()


# --orbit=D: turn the camera D degrees per frame around the view target, so
# temporal reuse (ReSTIR) sees camera motion. Fixed per frame, so a given
# --frames always ends at the same camera.
func _process(_delta: float) -> void:
	if not args.has("orbit"):
		return
	var target: Vector3 = VIEWS[view][1]
	var offset := camera.position - target
	camera.look_at_from_position(target + offset.rotated(Vector3.UP, deg_to_rad(float(args["orbit"]))), target)


func _update_hud() -> void:
	hud.text = "view: %s   path tracing: %s   spp %d   bounces %d   debug %d   denoiser %d   volumetric fog: %s\n1-8 views   P path tracing   D debug mode   R denoiser   F volumetric fog" % [
		view, "on" if env.pathtracing_enabled else "off",
		env.pathtracing_samples_per_pixel, env.pathtracing_max_bounces,
		env.pathtracing_debug_mode, env.pathtracing_denoiser,
		"on" if env.volumetric_fog_enabled else "off"]


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode >= KEY_1 and key.keycode <= KEY_8:
		args.erase("volfog")
		_set_view(VIEW_KEYS[key.keycode - KEY_1])
	elif key.keycode == KEY_P:
		env.pathtracing_enabled = not env.pathtracing_enabled
	elif key.keycode == KEY_D:
		env.pathtracing_debug_mode = (env.pathtracing_debug_mode + 1) % DEBUG_MODE_COUNT
	elif key.keycode == KEY_R:
		env.pathtracing_denoiser = 1 - env.pathtracing_denoiser
	elif key.keycode == KEY_F:
		env.volumetric_fog_enabled = not env.volumetric_fog_enabled
	_update_hud()


func _take_screenshot() -> void:
	for i in int(args.get("frames", "90")):
		await RenderingServer.frame_post_draw
	var path: String = args["shot"]
	var err := get_viewport().get_texture().get_image().save_png(path)
	print("Screenshot %s: %s" % [path, error_string(err)])
	get_tree().quit(0 if err == OK else 1)


# Prints the mean GPU and CPU render time over --bench=N frames, after
# --frames=M warm-up frames (custom hit groups compile in the background).
func _benchmark() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var vp := get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	for i in int(args.get("frames", "300")):
		await RenderingServer.frame_post_draw
	var count := int(args["bench"])
	var gpu := 0.0
	var cpu := 0.0
	for i in count:
		await RenderingServer.frame_post_draw
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp)
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp)
	var size := get_viewport().get_visible_rect().size
	print("BENCH view=%s pt=%s spp=%d bounces=%d ser=%s adaptive=%s@%s restir=%s res=%dx%d frames=%d gpu_ms=%.3f cpu_ms=%.3f" % [
		view, "1" if env.pathtracing_enabled else "0", env.pathtracing_samples_per_pixel,
		env.pathtracing_max_bounces, args.get("ser", "default"), args.get("adaptive", "0"), args.get("adaptive_threshold", "0.02"), args.get("restir", "0"), size.x, size.y, count,
		gpu / count, cpu / count])
	get_tree().quit()


# --- Building blocks --------------------------------------------------------

func _mat(p_albedo: Color, p_roughness := 0.5, p_metallic := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_albedo
	m.roughness = p_roughness
	m.metallic = p_metallic
	return m


func _emissive(p_color: Color, p_energy: float) -> StandardMaterial3D:
	var m := _mat(Color.BLACK, 1.0)
	m.emission_enabled = true
	m.emission = p_color
	m.emission_energy_multiplier = p_energy
	return m


func _mesh(p_mesh: Mesh, p_material: Material, p_pos: Vector3, p_rot_deg := Vector3.ZERO, p_name := "") -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.material_override = p_material
	mi.position = p_pos
	mi.rotation_degrees = p_rot_deg
	if p_name != "":
		mi.name = p_name
	add_child(mi)
	return mi


func _box(p_size: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = p_size
	return b


func _sphere(p_radius: float) -> SphereMesh:
	var s := SphereMesh.new()
	s.radius = p_radius
	s.height = p_radius * 2.0
	s.radial_segments = 48
	s.rings = 24
	return s


func _shader(p_path: String) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load(p_path)
	return m


func _label(p_text: String, p_pos: Vector3) -> void:
	var l := Label3D.new()
	l.text = p_text
	l.position = p_pos
	l.pixel_size = 0.006
	l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	l.outline_size = 8
	add_child(l)


# --- Areas -------------------------------------------------------------------

func _build_environment() -> void:
	var sky_mat := ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.volumetric_fog_density = 0.02
	env.volumetric_fog_albedo = Color(0.9, 0.9, 0.9)
	env.volumetric_fog_ambient_inject = 0.3
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -35, 0)
	sun.light_energy = 1.5
	sun.light_angular_distance = 0.5
	sun.shadow_enabled = true
	sun.light_volumetric_fog_energy = 1.0
	add_child(sun)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(80, 80)
	_mesh(plane, _mat(Color(0.5, 0.5, 0.5), 0.8), Vector3.ZERO, Vector3.ZERO, "Ground")


func _build_pbr(o: Vector3) -> void:
	# Columns: roughness 0 to 1. Rows: metallic 0 (bottom) to 1 (top).
	var sphere := _sphere(0.55)
	for row in 5:
		for col in 5:
			var metallic := row / 4.0
			var roughness := col / 4.0
			var albedo := Color(0.9, 0.9, 0.9).lerp(Color(1.0, 0.78, 0.34), metallic)
			_mesh(sphere, _mat(albedo, roughness, metallic), o + Vector3(-3.0 + col * 1.5, 0.8 + row * 1.3, 0))
	_label("roughness 0 -> 1 (left to right), metallic 0 -> 1 (bottom to top)", o + Vector3(0, 7.6, 0))

	# Normal map from generated noise.
	var noise := NoiseTexture2D.new()
	noise.noise = FastNoiseLite.new()
	noise.as_normal_map = true
	noise.bump_strength = 12.0
	noise.seamless = true
	var nm := _mat(Color(0.7, 0.2, 0.2), 0.4)
	nm.normal_enabled = true
	nm.normal_texture = noise
	_mesh(_sphere(0.8), nm, o + Vector3(5.2, 0.8, 0), Vector3.ZERO, "NormalMapped")

	# Textured albedo and ORM.
	var checker := NoiseTexture2D.new()
	checker.noise = FastNoiseLite.new()
	checker.noise.frequency = 0.05
	var tex := _mat(Color.WHITE, 1.0, 1.0)
	tex.albedo_texture = checker
	tex.roughness_texture = checker
	tex.metallic = 0.0
	_mesh(_sphere(0.8), tex, o + Vector3(-5.2, 0.8, 0), Vector3.ZERO, "Textured")


func _build_lighting(o: Vector3) -> void:
	# Cornell box, open toward +Z.
	var white := _mat(Color(0.8, 0.8, 0.8), 0.9)
	_mesh(_box(Vector3(6, 0.1, 6)), white, o + Vector3(0, 0.05, 0))
	_mesh(_box(Vector3(6, 0.1, 6)), white, o + Vector3(0, 5.0, 0))
	_mesh(_box(Vector3(6, 5, 0.1)), white, o + Vector3(0, 2.5, -3))
	_mesh(_box(Vector3(0.1, 5, 6)), _mat(Color(0.75, 0.1, 0.1), 0.9), o + Vector3(-3, 2.5, 0))
	_mesh(_box(Vector3(0.1, 5, 6)), _mat(Color(0.1, 0.65, 0.15), 0.9), o + Vector3(3, 2.5, 0))
	_mesh(_box(Vector3(1.4, 2.8, 1.4)), white, o + Vector3(-1.0, 1.5, -1.0), Vector3(0, 18, 0))
	_mesh(_sphere(0.8), _mat(Color(0.95, 0.95, 0.95), 0.05, 1.0), o + Vector3(1.2, 0.9, 0.6))

	var omni := OmniLight3D.new()
	omni.position = o + Vector3(0, 4.3, 0)
	omni.light_energy = 3.0
	omni.omni_range = 9.0
	omni.light_size = 0.4
	omni.shadow_enabled = true
	add_child(omni)

	var spot := SpotLight3D.new()
	spot.position = o + Vector3(2.2, 4.6, 2.0)
	spot.look_at_from_position(spot.position, o + Vector3(-1.0, 0.5, -1.0))
	spot.light_color = Color(0.4, 0.6, 1.0)
	spot.light_energy = 6.0
	spot.spot_range = 9.0
	spot.spot_angle = 22.0
	spot.shadow_enabled = true
	add_child(spot)
	_label("Cornell box: omni (white) + spot (blue)", o + Vector3(0, 5.6, 0))


func _build_emissive(o: Vector3) -> void:
	# Closed room, so only emissive meshes light the inside.
	var dark := _mat(Color(0.7, 0.7, 0.7), 0.8)
	_mesh(_box(Vector3(7, 0.1, 6)), dark, o + Vector3(0, 0.05, 0))
	_mesh(_box(Vector3(7, 0.1, 6)), dark, o + Vector3(0, 4.0, 0))
	_mesh(_box(Vector3(7, 4, 0.1)), dark, o + Vector3(0, 2, -3))
	_mesh(_box(Vector3(0.1, 4, 6)), dark, o + Vector3(-3.5, 2, 0))
	_mesh(_box(Vector3(0.1, 4, 6)), dark, o + Vector3(3.5, 2, 0))

	_mesh(_sphere(0.25), _emissive(Color(1.0, 0.5, 0.1), 40.0), o + Vector3(-2.2, 1.0, -1.0), Vector3.ZERO, "SmallHot")
	_mesh(_sphere(0.6), _emissive(Color(0.2, 0.5, 1.0), 6.0), o + Vector3(0, 1.0, -1.5), Vector3.ZERO, "MediumBlue")
	_mesh(_box(Vector3(2.0, 0.05, 1.0)), _emissive(Color(1, 1, 1), 8.0), o + Vector3(0, 3.95, 0), Vector3.ZERO, "CeilingPanel")
	_mesh(_box(Vector3(0.1, 1.5, 0.1)), _emissive(Color(1.0, 0.1, 0.6), 15.0), o + Vector3(2.4, 1.0, -0.5), Vector3.ZERO, "NeonTube")
	_mesh(_sphere(0.6), _mat(Color(0.9, 0.9, 0.9), 0.3), o + Vector3(1.0, 0.6, 0.5))
	_label("lit only by emissive meshes", o + Vector3(0, 4.5, 1.5))


func _build_shaders(o: Vector3) -> void:
	_mesh(_sphere(0.8), _shader("res://shaders/checker_glow.gdshader"), o + Vector3(-3.6, 0.8, 0), Vector3.ZERO, "CheckerGlow")
	_mesh(_sphere(0.8), _shader("res://shaders/wave_normal.gdshader"), o + Vector3(-1.2, 0.8, 0), Vector3.ZERO, "WaveNormal")
	var quad := QuadMesh.new()
	quad.size = Vector2(1.8, 1.8)
	_mesh(quad, _shader("res://shaders/leaf_cutout.gdshader"), o + Vector3(1.2, 1.0, 0), Vector3.ZERO, "LeafCutout")
	var strip := PlaneMesh.new()
	strip.size = Vector2(2.0, 1.4)
	strip.subdivide_width = 64
	_mesh(strip, _shader("res://shaders/vertex_wave.gdshader"), o + Vector3(3.6, 0.6, 0), Vector3(0, 0, 0), "VertexWave")
	_label("checker+glow | normal map | alpha cutout | vertex wave", o + Vector3(0, 2.4, 0))


func _build_glass(o: Vector3) -> void:
	var backdrop := _mat(Color(0.9, 0.9, 0.9), 0.9)
	_mesh(_box(Vector3(8, 4, 0.1)), backdrop, o + Vector3(0, 2, -2))
	for i in 6:
		_mesh(_box(Vector3(0.3, 3.5, 0.3)), _mat(Color.from_hsv(i / 6.0, 0.8, 0.9), 0.5), o + Vector3(-3.0 + i * 1.2, 1.75, -1.5))

	var alpha := _mat(Color(0.3, 0.6, 1.0, 0.35), 0.1)
	alpha.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_mesh(_sphere(0.8), alpha, o + Vector3(-2.4, 0.9, 0.5), Vector3.ZERO, "AlphaBlend")

	var refract := _mat(Color(1, 1, 1, 0.1), 0.0)
	refract.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	refract.refraction_enabled = true
	refract.refraction_scale = 0.1
	_mesh(_sphere(0.8), refract, o + Vector3(0, 0.9, 0.5), Vector3.ZERO, "Refraction")

	var scissor := _mat(Color(0.9, 0.5, 0.1, 0.4), 0.5)
	scissor.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	scissor.alpha_scissor_threshold = 0.3
	_mesh(_sphere(0.8), scissor, o + Vector3(2.4, 0.9, 0.5), Vector3.ZERO, "ScissorThreshold0.3")
	_label("alpha blend | refraction | alpha scissor 0.3 (should be visible)", o + Vector3(0, 2.6, 0.5))


func _build_many_lights(o: Vector3) -> void:
	# Closed room (open towards the camera) with 48 small colored omni lights
	# between pillars: direct light from many lights, each lighting a small area.
	var wall := _mat(Color(0.75, 0.75, 0.75), 0.7)
	_mesh(_box(Vector3(10, 0.1, 8)), wall, o + Vector3(0, 0.05, 0))
	_mesh(_box(Vector3(10, 0.1, 8)), wall, o + Vector3(0, 4.0, 0))
	_mesh(_box(Vector3(10, 4, 0.1)), wall, o + Vector3(0, 2, -4))
	_mesh(_box(Vector3(0.1, 4, 8)), wall, o + Vector3(-5, 2, 0))
	_mesh(_box(Vector3(0.1, 4, 8)), wall, o + Vector3(5, 2, 0))
	for x in 4:
		for z in 2:
			_mesh(_box(Vector3(0.4, 4, 0.4)), _mat(Color(0.8, 0.8, 0.8), 0.5), o + Vector3(-3.6 + x * 2.4, 2, -2.2 + z * 2.4))
	_mesh(_sphere(0.5), _mat(Color(0.9, 0.9, 0.9), 0.2, 1.0), o + Vector3(0, 0.5, 1.5))
	_label("48 omni lights", o + Vector3(0, 4.5, 3.0))
	# The 48 lights count in every view (light sampling sees all lights), so
	# screenshot and benchmark runs of other views leave them out; that keeps
	# their numbers comparable with the baselines in PATHTRACER_TESTING.md.
	var measuring := args.has("shot") or args.has("bench")
	if measuring and args.get("view", "overview") != "lights":
		return
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 48:
		var light := OmniLight3D.new()
		light.position = o + Vector3(rng.randf_range(-4.6, 4.6), rng.randf_range(0.3, 3.6), rng.randf_range(-3.6, 3.0))
		light.light_color = Color.from_hsv(rng.randf(), 0.7, 1.0)
		light.light_energy = 1.5
		light.omni_range = 2.5
		light.light_size = 0.05
		light.shadow_enabled = true
		add_child(light)


func _build_fog(o: Vector3) -> void:
	# Pillars to cast light shafts, a FogVolume box, and a spot light through it.
	for i in 5:
		_mesh(_box(Vector3(0.4, 4, 0.4)), _mat(Color(0.6, 0.6, 0.6), 0.8), o + Vector3(-3.0 + i * 1.5, 2, -1.0))

	var volume := FogVolume.new()
	volume.size = Vector3(4, 3, 4)
	volume.position = o + Vector3(0, 1.5, 1.5)
	var fog_mat := FogMaterial.new()
	fog_mat.density = 0.6
	fog_mat.albedo = Color(1.0, 0.85, 0.6)
	volume.material = fog_mat
	add_child(volume)

	var spot := SpotLight3D.new()
	spot.position = o + Vector3(0, 6, 4)
	spot.look_at_from_position(spot.position, o + Vector3(0, 0, 0))
	spot.light_color = Color(1.0, 0.9, 0.7)
	spot.light_energy = 10.0
	spot.spot_range = 14.0
	spot.spot_angle = 20.0
	spot.shadow_enabled = true
	spot.light_volumetric_fog_energy = 4.0
	add_child(spot)
	_label("volumetric fog + FogVolume + spot shaft", o + Vector3(0, 4.8, 0))
