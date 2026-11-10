extends Node3D
## Native renderer owns upload, GPU selection and indirect submission.

signal profile_checked(selection_count: int, hzb_count: int)
const ENABLED := "rendering/geometry/cluster_lod/enabled"
const ERROR := "rendering/geometry/cluster_lod/error_pixels"
const OCCLUSION := "rendering/geometry/cluster_lod/occlusion_enabled"
var _failures: Array[String] = []
var _mesh: ArrayMesh
var _instance: MeshInstance3D
var _camera: Camera3D
var _secondary: SubViewport
var _shadow_view: SubViewport
var _wall: MeshInstance3D
var _expect_hzb := true


func _ready() -> void:
	ProjectSettings.set_setting(OCCLUSION, true)
	# The HZB timestamp counts every build of the shared pyramid; only CLOD
	# may build it here (the GPU path and MultiMesh occlusion are on by default).
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/occlusion", false)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/occlusion", false)
	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	_camera.size = 12
	_camera.position = Vector3(0, 0, 12)
	add_child(_camera)
	_camera.current = true
	_secondary = SubViewport.new()
	_secondary.size = Vector2i(320, 180)
	_secondary.world_3d = get_world_3d()
	_secondary.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_secondary)
	var other := Camera3D.new()
	other.position = Vector3(4, 3, 12)
	_secondary.add_child(other)
	other.look_at(Vector3.ZERO)
	other.current = true
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-60, 30, 0)
	light.shadow_enabled = true
	add_child(light)
	var floor_node := MeshInstance3D.new()
	var floor_mesh := BoxMesh.new()
	floor_mesh.size = Vector3(12, 0.1, 12)
	floor_node.mesh = floor_mesh
	floor_node.position.y = -2.2
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.7, 0.7, 0.7)
	floor_node.material_override = floor_material
	add_child(floor_node)
	_wall = MeshInstance3D.new()
	var wall_mesh := BoxMesh.new()
	wall_mesh.size = Vector3(3, 5, 0.1)
	_wall.mesh = wall_mesh
	_wall.position = Vector3(-1.8, 0, 2)
	var wall_material := StandardMaterial3D.new()
	wall_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	wall_material.albedo_color = Color(0.6, 0.4, 0.2)
	_wall.material_override = wall_material
	add_child(_wall)
	_shadow_view = SubViewport.new()
	_shadow_view.size = Vector2i(320, 320)
	_shadow_view.world_3d = get_world_3d()
	_shadow_view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_shadow_view)
	var shadow_camera := Camera3D.new()
	shadow_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	shadow_camera.size = 12
	shadow_camera.position = Vector3(0, 10, 0)
	_shadow_view.add_child(shadow_camera)
	shadow_camera.look_at(Vector3.ZERO, Vector3.FORWARD)
	shadow_camera.current = true
	_mesh = ArrayMesh.new()
	for surface in 2:
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		var vertices := PackedVector3Array()
		var uv := PackedVector2Array()
		var indices := PackedInt32Array()
		const EDGE := 48
		for y in EDGE + 1:
			for x in EDGE + 1:
				vertices.append(Vector3(float(x) / EDGE * 4 - 4 + surface * 4, float(y) / EDGE * 4 - 2, sin(x * 0.2) * cos(y * 0.2) * 0.5))
				uv.append(Vector2(x, y) / EDGE)
		for y in EDGE:
			for x in EDGE:
				var a := y * (EDGE + 1) + x
				indices.append_array(PackedInt32Array([a, a + EDGE + 1, a + 1, a + 1, a + EDGE + 1, a + EDGE + 2]))
		arrays[Mesh.ARRAY_VERTEX] = vertices
		arrays[Mesh.ARRAY_TEX_UV] = uv
		arrays[Mesh.ARRAY_INDEX] = indices
		_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var shader := Shader.new()
		shader.code = "shader_type spatial; render_mode unshaded, cull_disabled; void fragment() { ALBEDO = vec3(UV.x, UV.y, 0.2); }"
		var material := ShaderMaterial.new()
		material.shader = shader
		_mesh.surface_set_material(surface, material)
		if surface == 0:
			var standard := StandardMaterial3D.new()
			standard.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			standard.cull_mode = BaseMaterial3D.CULL_DISABLED
			standard.albedo_color = Color(0.15, 0.65, 0.9)
			_mesh.surface_set_material(surface, standard)
	# A distinct shadow mesh reverses vertex numbering while preserving geometry.
	# Main CLOD depth must never bind its buffers with source hierarchy indices.
	var shadow_mesh := ArrayMesh.new()
	for surface in _mesh.get_surface_count():
		var source_arrays := _mesh.surface_get_arrays(surface)
		var source_positions: PackedVector3Array = source_arrays[Mesh.ARRAY_VERTEX]
		var shadow_positions := PackedVector3Array()
		shadow_positions.resize(source_positions.size())
		for vertex in source_positions.size():
			shadow_positions[source_positions.size() - vertex - 1] = source_positions[vertex]
		var shadow_indices := PackedInt32Array()
		for index in source_arrays[Mesh.ARRAY_INDEX]:
			shadow_indices.append(source_positions.size() - index - 1)
		var shadow_arrays: Array = []
		shadow_arrays.resize(Mesh.ARRAY_MAX)
		shadow_arrays[Mesh.ARRAY_VERTEX] = shadow_positions
		shadow_arrays[Mesh.ARRAY_INDEX] = shadow_indices
		shadow_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, shadow_arrays)
	_mesh.shadow_mesh = shadow_mesh
	if _mesh.generate_cluster_lods() != OK:
		_failures.append("native hierarchy generation failed")
		_finish()
		return
	_instance = MeshInstance3D.new()
	_instance.mesh = _mesh
	add_child(_instance)
	ProjectSettings.set_setting(ERROR, 0.0)
	await _compare("fine cut, two materials/surfaces and two viewports")
	_wall.position.x = 20
	await _compare("current-frame recovery after occluder moves away")
	_wall.position.x = -1.8
	await _compare("current-frame occluder returns")
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.position.x = 1
	_instance.scale = Vector3(-1.0, 1.25, 0.8)
	_secondary.size = Vector2i(384, 216)
	await _compare("perspective, reflected nonuniform transform and viewport resize")
	var coarse_source := await _image(false, false)
	var shadow_reference := _shadow_view.get_texture().get_image()
	var shadow_draws := get_viewport().get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME)
	ProjectSettings.set_setting(ERROR, 1000000.0)
	var coarse := await _image(true, true)
	if _coloured(coarse) < 500:
		_failures.append("coarse native cut missing geometry")
	if coarse.get_data() == coarse_source.get_data():
		_failures.append("coarse selection did not change native raster pixels")
	if shadow_draws == 0 or shadow_draws != get_viewport().get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME):
		_failures.append("conventional shadow submission missing or changed with CLOD")
	var shadow_actual := _shadow_view.get_texture().get_image()
	# Top-down receiver crop excludes all source geometry (its local z is
	# within +/-0.5). Only the source's cast shadow can alter this floor strip.
	var a := shadow_camera.unproject_position(Vector3(-5, -2.1, -4))
	var b := shadow_camera.unproject_position(Vector3(5, -2.1, -1))
	var crop := Rect2i(Vector2i(a.min(b)), Vector2i((b - a).abs()))
	var darkest := 1.0
	var brightest := 0.0
	for y in range(crop.position.y, crop.end.y, 2):
		for x in range(crop.position.x, crop.end.x, 2):
			var brightness := shadow_reference.get_pixel(x, y).r
			darkest = minf(darkest, brightness)
			brightest = maxf(brightest, brightness)
	if brightest - darkest < 0.03:
		_failures.append("shadow receiver crop has no measurable shadow")
	if shadow_reference.get_region(crop).get_data() != shadow_actual.get_region(crop).get_data():
		_failures.append("coarse camera CLOD changed conventional cast-shadow receiver pixels")
	ProjectSettings.set_setting(ERROR, 0.0)
	await _compare("return to fine cut after coarse selection")
	# Invalidate while CLOD outputs from preceding frames remain submitted.
	var source: PackedByteArray = _mesh.get("_surfaces")[0].vertex_data
	_mesh.surface_update_vertex_region(0, 0, source)
	await _compare("mutation restores conventional raster", false)
	if _mesh.generate_cluster_lods() != OK:
		_failures.append("recook failed")
	await _compare("resident buffers recreated after source mutation")
	_expect_hzb = false
	for viewport in [get_viewport(), _secondary, _shadow_view]:
		viewport.msaa_3d = Viewport.MSAA_2X
	await _compare("MSAA retains CLOD but disables HZB")
	for viewport in [get_viewport(), _secondary, _shadow_view]:
		viewport.msaa_3d = Viewport.MSAA_DISABLED
	_expect_hzb = true
	var raw_indices: PackedByteArray = _mesh.get("_surfaces")[0].index_data
	RenderingServer.mesh_surface_update_index_region(_mesh.get_rid(), 0, 0, raw_indices)
	await _compare("raw index update invalidates affected RD surface")
	if _mesh.generate_cluster_lods() != OK:
		_failures.append("recook after raw index update failed")
	var displaced := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = "shader_type spatial; render_mode unshaded, cull_disabled; void vertex() { VERTEX.x += 0.1; } void fragment() { ALBEDO = vec3(0.8, UV.y, 0.3); }"
	displaced.shader = shader
	_instance.material_override = displaced
	await _compare("custom vertex transformation fallback", false)
	shader.code = "shader_type spatial; render_mode unshaded, cull_disabled; void fragment() { ALBEDO = vec3(0.8, UV.y, 0.3); }"
	var next_material := StandardMaterial3D.new()
	next_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	next_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	next_material.albedo_color = Color(0.4, 0.8, 0.2)
	displaced.next_pass = next_material
	await _compare("material next-pass chain fallback", false)
	displaced.next_pass = null
	shader.code = "shader_type spatial; render_mode unshaded, cull_disabled; void fragment() { ALBEDO = vec3(0.8, UV.y, 0.3); DEPTH = FRAGCOORD.z; }"
	_expect_hzb = false
	await _compare("custom fragment depth disables viewport HZB")
	_instance.material_override = null
	_expect_hzb = true
	var cameras: Array[Camera3D] = [_camera, _secondary.get_camera_3d(), _shadow_view.get_camera_3d()]
	var camera_transforms: Array[Transform3D] = []
	for camera in cameras:
		camera_transforms.append(camera.global_transform)
	var shift := Vector3(100000, 0, 0)
	position += shift
	for i in cameras.size():
		var shifted := camera_transforms[i]
		shifted.origin += shift
		cameras[i].global_transform = shifted
	await _compare("large cancelling world coordinates retain uncertain groups")
	_instance.queue_free()
	_mesh = null
	for frame in 4:
		await RenderingServer.frame_post_draw
	_finish()


func _read_profile() -> void:
	var rd := RenderingServer.get_rendering_device()
	var count := 0
	var hzb_count := 0
	for i in rd.get_captured_timestamps_count():
		if rd.get_captured_timestamp_name(i) == "Mesh CLOD Selection":
			count += 1
		if rd.get_captured_timestamp_name(i) == "Mesh CLOD HZB":
			hzb_count += 1
	profile_checked.emit.call_deferred(count, hzb_count)


func _image(enabled: bool, expect_active: bool) -> Image:
	ProjectSettings.set_setting(ENABLED, enabled)
	for frame in 10:
		await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(_read_profile)
	var profile: Array = await profile_checked
	var count: int = profile[0]
	var hzb_count: int = profile[1]
	if enabled and expect_active and count < 3:
		_failures.append("native CLOD kernel missing in one or more viewports")
	if (not enabled or not expect_active) and count != 0:
		_failures.append("native CLOD ran despite fallback/disabled state")
	if enabled and expect_active and _expect_hzb and hzb_count < 3:
		_failures.append("current-frame HZB did not run for all viewports")
	if (not enabled or not expect_active or not _expect_hzb) and hzb_count != 0:
		_failures.append("HZB ran despite fallback/disabled state")
	return get_viewport().get_texture().get_image()


func _coloured(image: Image) -> int:
	var count := 0
	for y in range(0, image.get_height(), 2):
		for x in range(0, image.get_width(), 2):
			var pixel := image.get_pixel(x, y)
			if pixel.r + pixel.g + pixel.b > 0.3:
				count += 1
	return count


func _compare(label: String, expect_active: bool = true) -> void:
	var reference := await _image(false, false)
	var other_reference := _secondary.get_texture().get_image()
	var actual := await _image(true, expect_active)
	var other_actual := _secondary.get_texture().get_image()
	if actual.get_data() != reference.get_data() or other_actual.get_data() != other_reference.get_data():
		_failures.append("native CLOD/source fine-cut pixels differ: " + label)
	if _coloured(reference) < 500:
		_failures.append("empty source reference: " + label)


func _finish() -> void:
	ProjectSettings.set_setting(ENABLED, false)
	ProjectSettings.set_setting(OCCLUSION, false)
	if _failures.is_empty():
		print("CLOD_RASTER_TEST_PASS: native resident CLOD/HZB materials, fine/coarse cuts, current-frame occluder recovery, three views, shadows, transforms, resize, mutation, MSAA/depth/multipass fallback and destruction")
	else:
		for failure in _failures:
			printerr("CLOD_RASTER_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
