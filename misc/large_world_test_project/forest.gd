extends Node3D
## Dense, statically lit native scene. No script-side visibility/draw submission.

signal profile_ready(culling: int, clod: int, hzb: int, shadow_culling: int)
const TREE_COUNT := 6144
const ROCK_COUNT := 48
var _failures: Array[String] = []
var _camera: Camera3D
var _overview := false


func _material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.85
	return material


func _tree_mesh() -> ArrayMesh:
	var result := ArrayMesh.new()
	for part in 2:
		var cylinder := CylinderMesh.new()
		cylinder.radial_segments = 8 if part == 0 else 10
		cylinder.rings = 1
		cylinder.top_radius = 0.13 if part == 0 else 0.0
		cylinder.bottom_radius = 0.22 if part == 0 else 1.25
		cylinder.height = 2.1 if part == 0 else 3.8
		var arrays := cylinder.get_mesh_arrays()
		var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		for i in positions.size():
			positions[i].y += 1.05 if part == 0 else 3.1
		arrays[Mesh.ARRAY_VERTEX] = positions
		result.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var material := _material(Color(0.28, 0.12, 0.045) if part == 0 else Color(0.13, 0.44, 0.095))
		material.vertex_color_use_as_albedo = true
		result.surface_set_material(part, material)
	return result


func _ready() -> void:
	get_window().size = Vector2i(1280, 720)
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color(0.18, 0.29, 0.43)
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color = Color(0.45, 0.58, 0.75)
	settings.ambient_light_energy = 0.28
	environment.environment = settings
	add_child(environment)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52, -28, 0)
	sun.light_color = Color(1.0, 0.86, 0.67)
	sun.light_energy = 1.6
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 150
	add_child(sun)
	var lamps: Array[OmniLight3D] = []
	for i in 2:
		var lamp := OmniLight3D.new()
		lamp.position = Vector3(-6 if i == 0 else 5, 4, -4 if i == 0 else 8)
		lamp.light_color = Color(1.0, 0.25, 0.055) if i == 0 else Color(0.1, 0.4, 1.0)
		lamp.light_energy = 8
		lamp.omni_range = 18
		lamp.shadow_enabled = true
		add_child(lamp)
		lamps.append(lamp)
	var ground := MeshInstance3D.new()
	var ground_mesh := BoxMesh.new()
	ground_mesh.size = Vector3(250, 0.5, 190)
	ground.mesh = ground_mesh
	ground.position.y = -0.25
	ground.material_override = _material(Color(0.24, 0.21, 0.12))
	add_child(ground)
	var random := RandomNumberGenerator.new()
	random.seed = 314159
	var forest := MultiMesh.new()
	forest.transform_format = MultiMesh.TRANSFORM_3D
	forest.use_colors = true
	forest.mesh = _tree_mesh()
	forest.instance_count = TREE_COUNT
	for i in TREE_COUNT:
		var x := (float(i % 96) - 47.5) * 2.3 + random.randf_range(-0.4, 0.4)
		var z := (float(i / 96) - 31.5) * 2.3 + random.randf_range(-0.4, 0.4)
		if absf(x) < 3.5:
			x += 7.0 # Keep a lit stone-lined clearing visible to both cameras.
		var scale_factor := random.randf_range(0.65, 1.35)
		var basis := Basis(Vector3.UP, random.randf_range(0, TAU)).scaled(Vector3.ONE * scale_factor)
		forest.set_instance_transform(i, Transform3D(basis, Vector3(x, 0, z)))
		forest.set_instance_color(i, Color(random.randf_range(0.7, 1.0), random.randf_range(0.75, 1.0), random.randf_range(0.65, 1.0)))
	var trees := MultiMeshInstance3D.new()
	trees.multimesh = forest
	add_child(trees)
	var sphere := SphereMesh.new()
	sphere.radial_segments = 40
	sphere.rings = 20
	var rock_mesh := ArrayMesh.new()
	rock_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	if rock_mesh.generate_cluster_lods() != OK:
		_failures.append("rock cluster cooker failed")
	for i in ROCK_COUNT:
		var rock := MeshInstance3D.new()
		rock.mesh = rock_mesh
		rock.position = Vector3(-2.8 if i % 2 == 0 else 2.8, 0.5, -30 + i * 1.25)
		rock.scale = Vector3(random.randf_range(0.7, 1.4), random.randf_range(0.7, 1.5), random.randf_range(0.7, 1.3))
		rock.rotation.y = random.randf_range(0, TAU)
		# Distinct native materials avoid automatic batching, currently excluded by CLOD.
		rock.material_override = _material(Color(random.randf_range(0.3, 0.6), 0.32, 0.26))
		add_child(rock)
	_camera = Camera3D.new()
	_camera.position = Vector3(13, 10, 28)
	_camera.far = 250
	add_child(_camera)
	_camera.look_at(Vector3(0, 2, -15))
	_camera.current = true
	var last_reference: Image
	for view in 3:
		# View 0 sees the whole forest: the MultiMesh lies inside every frustum
		# plane, so GPU culling must be skipped (direct draw) while CLOD still runs.
		# The last view stays the close-up used by the light/shadow controls.
		_overview = view == 0
		if view == 0:
			_camera.far = 700
			_camera.position = Vector3(0, 190, 260)
			_camera.look_at(Vector3(0, 0, -10))
		elif view == 1:
			_camera.far = 250
			_camera.position = Vector3(13, 10, 28)
			_camera.look_at(Vector3(0, 2, -15))
		else:
			_camera.position = Vector3(-16, 16, 16)
			_camera.look_at(Vector3(0, 1, -10))
		var reference := await _capture(false)
		var optimized := await _capture(true)
		var repeat := await _capture(false)
		last_reference = repeat
		if reference.get_data() != repeat.get_data():
			_failures.append("reference lighting/image not stable at view %d" % view)
		if reference.get_data() != optimized.get_data():
			_failures.append("source versus culling/CLOD/HZB pixels differ at view %d" % view)
		var driver := RenderingServer.get_current_rendering_driver_name()
		var output := ProjectSettings.globalize_path("res://../../devtools/out/forest")
		DirAccess.make_dir_recursive_absolute(output)
		if reference.save_png(output.path_join("%s_view%d_reference.png" % [driver, view])) != OK or optimized.save_png(output.path_join("%s_view%d_enabled.png" % [driver, view])) != OK:
			_failures.append("screenshot save failed")
		var dark := 0
		var bright := 0
		for y in range(0, reference.get_height(), 4):
			for x in range(0, reference.get_width(), 4):
				var pixel := reference.get_pixel(x, y)
				dark += int(pixel.get_luminance() < 0.12)
				bright += int(pixel.get_luminance() > 0.3)
		if dark < 100 or bright < 100:
			_failures.append("scene lacks measurable shaded/lit coverage")
		# The overview lies beyond the 150 m directional shadow distance.
		if view > 0 and get_viewport().get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME) == 0:
			_failures.append("shadow geometry was not submitted")
	# TAA jitters the projection every frame, and culling must still keep every
	# instance the jittered raster touches. TAA history is not bit-stable across
	# captures, so the culled capture is compared against source-to-source noise.
	# 96 frames keep the same position in the 16-frame jitter sequence.
	get_viewport().use_taa = true
	var taa_reference := await _capture(false, 96)
	var taa_optimized := await _capture(true, 96)
	var taa_repeat := await _capture(false, 96)
	get_viewport().use_taa = false
	var taa_noise := _differing_pixels(taa_reference, taa_repeat)
	var taa_error := _differing_pixels(taa_reference, taa_optimized)
	if taa_error > taa_noise + taa_reference.get_width() * taa_reference.get_height() / 2000:
		_failures.append("with TAA, culling changed %d pixels (source repeat: %d)" % [taa_error, taa_noise])
	# Negative controls: equality alone could accept broken lighting on both paths.
	for lamp in lamps:
		lamp.visible = false
	var locals_off := await _capture(false)
	if locals_off.get_data() == last_reference.get_data():
		_failures.append("local lights made no measurable image contribution")
	for lamp in lamps:
		lamp.visible = true
	sun.shadow_enabled = false
	var shadows_off := await _capture(false)
	if shadows_off.get_data() == last_reference.get_data():
		_failures.append("directional shadows made no measurable image contribution")
	var output := ProjectSettings.globalize_path("res://../../devtools/out/forest")
	var driver := RenderingServer.get_current_rendering_driver_name()
	if locals_off.save_png(output.path_join("%s_locals_off.png" % driver)) != OK or shadows_off.save_png(output.path_join("%s_sun_shadows_off.png" % driver)) != OK:
		_failures.append("lighting control screenshot save failed")
	if _failures.is_empty():
		print("FOREST_TEST_PASS: %d trees, %d CLOD rocks, ground, directional + 2 local shadow lights; three views (one fully visible, culling skipped), shadow-pass culling dispatched, source/enabled/repeat pixel-identical; TAA culled/source %d px vs repeat %d px; light/shadow negative controls differ" % [TREE_COUNT, ROCK_COUNT, taa_error, taa_noise])
	else:
		for failure in _failures:
			printerr("FOREST_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)


func _read_profile() -> void:
	var rd := RenderingServer.get_rendering_device()
	var counts := [0, 0, 0, 0]
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if name == "MultiMesh Frustum Culling": counts[0] += 1
		if name == "Mesh CLOD Selection": counts[1] += 1
		if name == "Mesh CLOD HZB": counts[2] += 1
		if name == "MultiMesh Shadow Culling": counts[3] += 1
	profile_ready.emit.call_deferred(counts[0], counts[1], counts[2], counts[3])


func _capture(enabled: bool, frames := 32) -> Image:
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", enabled)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/shadows", enabled)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", enabled)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", 0.0)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", enabled)
	for frame in frames:
		await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(_read_profile)
	var profile: Array = await profile_ready
	for stage in profile.size():
		var count: int = profile[stage]
		if enabled and _overview and stage == 0:
			if count != 0:
				_failures.append("fully visible MultiMesh still dispatched GPU culling")
		elif enabled and _overview and stage == 3:
			pass # The overview lies beyond the directional shadow distance.
		elif enabled and count < 1:
			_failures.append("enabled forest did not dispatch all GPU stages")
		if not enabled and count != 0:
			_failures.append("disabled forest still dispatched experimental GPU stages")
	return get_viewport().get_texture().get_image()


func _differing_pixels(a: Image, b: Image) -> int:
	# Pixels with any channel more than 2/255 apart.
	var count := 0
	for y in a.get_height():
		for x in a.get_width():
			var p := a.get_pixel(x, y)
			var q := b.get_pixel(x, y)
			count += int(maxf(maxf(absf(p.r - q.r), absf(p.g - q.g)), absf(p.b - q.b)) > 2.0 / 255.0)
	return count
