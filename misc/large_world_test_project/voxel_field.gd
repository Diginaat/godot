extends Node3D
## VoxelField3D / VoxelProxyBuilder.build_field: exact level statistics for
## known inputs and determinism; the GPU picks the same level per brick as
## VoxelFieldData.select_level and writes exactly the selected voxels (checked
## against the instance buffer) at several distances and orthogonally;
## frustum culling, the splat cap and an empty field; a distant render of a
## MultiMesh grove against its source (coverage and mean colour).

var _failures: Array[String] = []
var _camera: Camera3D
var _field: VoxelField3D


func _plane(size: float, material: Material) -> ArrayMesh:
	var vertices := PackedVector3Array([Vector3(0, 0, 0), Vector3(size, 0, 0), Vector3(size, 0, size), Vector3(0, 0, size)])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 0, 3, 2])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	return mesh


func _near(a: Variant, b: Variant, tolerance: float) -> bool:
	if a is Vector3:
		return (a - b).length() <= tolerance
	if a is Color:
		return absf(a.r - b.r) <= tolerance and absf(a.g - b.g) <= tolerance and absf(a.b - b.b) <= tolerance
	return absf(a - b) <= tolerance


func _check_data() -> void:
	var red := StandardMaterial3D.new()
	red.albedo_color = Color(1, 0, 0)
	var builder := VoxelProxyBuilder.new()
	builder.sample_spacing = 0.25
	# 1. A flat 1 m square filling one 1 m voxel of a 4 m field with one brick:
	# levels of 1, 2 and 4 m voxels, one voxel each. Coverage falls by 4 per
	# level and the splat half size stays 0.5 m: the square's own extent.
	builder.add_mesh(_plane(1.0, red), Transform3D(Basis(), Vector3(1, 0.5, 1)))
	var data := builder.build_field(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4, 1)
	if data == null or not data.is_valid() or data.get_level_count() != 3 or data.get_brick_count() != 1 or data.get_voxel_count() != 3:
		_failures.append("flat square: levels %s, bricks %s, voxels %s" % [data.get_level_count() if data else -1, data.get_brick_count() if data else -1, data.get_voxel_count() if data else -1])
		return
	for level in 3:
		if data.get_level_voxel_count(level) != 1 or not _near(data.get_level_voxel_size(level), [1.0, 2.0, 4.0][level], 1e-6):
			_failures.append("flat square: level %d has %d voxels of %.3f m" % [level, data.get_level_voxel_count(level), data.get_level_voxel_size(level)])
	# Centroids are stored in 1/16 of a voxel: at most 1/32 voxel off per axis.
	for i in 3:
		var voxel := data.get_voxel(i)
		if voxel.level != i or not _near(voxel.position, Vector3(1.5, 0.5, 1.5), [1.0, 2.0, 4.0][i] * sqrt(3.0) / 32.0 + 0.002) or not _near(voxel.normal, Vector3.UP, 0.001) or not _near(voxel.color, Color(1, 0, 0), 0.001) or not _near(voxel.half_size, 0.5, 0.01):
			_failures.append("flat square: voxel %d %s" % [i, voxel])
	# 2. Four 1 m squares in four corners of an 8 m field with 4 bricks per
	# axis: four bricks of one voxel on each of 2 levels (8 and 4 voxels per
	# axis), and identical repeated builds.
	builder.clear()
	for corner in [Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3(0, 0, 4), Vector3(4, 0, 4)]:
		builder.add_mesh(_plane(1.0, red), Transform3D(Basis(), corner + Vector3(0, 0.5, 0)))
	data = builder.build_field(AABB(Vector3.ZERO, Vector3(8, 8, 8)), 8, 4)
	var again := builder.build_field(AABB(Vector3.ZERO, Vector3(8, 8, 8)), 8, 4)
	if data.get_brick_count() != 4 or data.get_level_count() != 2 or data.get_voxel_count() != 8:
		_failures.append("quadrants: %d bricks, %d levels, %d voxels" % [data.get_brick_count(), data.get_level_count(), data.get_voxel_count()])
	if data.get_voxels() != again.get_voxels() or data.get_bricks() != again.get_bricks():
		_failures.append("repeated builds differ")
	# 3. Normals survive the octahedral encoding.
	builder.clear()
	var tilted := Transform3D(Basis(Vector3(1, 0, 1).normalized(), 2.2), Vector3(2, 2, 2))
	builder.add_mesh(_plane(1.0, red), tilted)
	data = builder.build_field(AABB(Vector3.ZERO, Vector3(4, 4, 4)), 4, 4)
	var expected_normal := (tilted.basis * Vector3.UP).normalized()
	# The plane winds upward, so its face normal is +Y before the rotation.
	if data.get_voxel_count() == 0 or data.get_voxel(0).normal.dot(expected_normal) < 0.9999:
		_failures.append("normal %s against %s" % [data.get_voxel(0).normal if data.get_voxel_count() else null, expected_normal])
	# 4. Inconsistent data is rejected.
	var broken := VoxelFieldData.new()
	broken.resolution = 8
	broken.brick_resolution = 2
	broken.bricks = PackedInt32Array([0, 0, 5, 0, 1, 0, 1])
	if broken.is_valid():
		_failures.append("a brick range past the voxel data was accepted")


func _grove() -> MultiMesh:
	# Small boxes in a 40 m square: a stand-in for a far forest patch.
	var box := BoxMesh.new()
	box.size = Vector3(1.2, 4.0, 1.2)
	var material := StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	box.material = material
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = box
	multimesh.instance_count = 400
	var random := RandomNumberGenerator.new()
	random.seed = 5
	for i in 400:
		multimesh.set_instance_transform(i, Transform3D(Basis(Vector3.UP, random.randf() * TAU), Vector3(random.randf_range(-20, 20), 2.0, random.randf_range(-20, 20))))
		multimesh.set_instance_color(i, Color(random.randf_range(0.1, 0.3), random.randf_range(0.4, 0.7), 0.15))
	return multimesh


func _count() -> int:
	# The count is read back asynchronously; wait until it is stable.
	var last := -1
	var stable := 0
	for frame in 120:
		await RenderingServer.frame_post_draw
		var count := _field.get_splat_count()
		stable = stable + 1 if count == last else 0
		last = count
		if stable >= 8:
			return count
	_failures.append("splat count did not settle")
	return last


func _pixel_scale() -> float:
	return 0.5 * get_viewport().get_visible_rect().size.y * _camera.get_camera_projection().y.y


func _sorted(values: Array) -> Array:
	values.sort_custom(func(a: Vector4, b: Vector4) -> bool: return [a.x, a.y, a.z, a.w] < [b.x, b.y, b.z, b.w])
	return values


func _expected(data: VoxelFieldData, orthogonal: bool) -> Dictionary:
	# Splats the CPU rule selects, as (centre, half size), and the levels used.
	var bricks := data.get_bricks()
	var stride := 1 + 2 * data.get_level_count()
	var splats := []
	var levels := {}
	var eye := _field.to_local(_camera.global_position)
	for brick in data.get_brick_count():
		var level := data.select_level(brick, eye, _pixel_scale(), orthogonal, _field.voxel_pixels)
		levels[level] = true
		var first := bricks[brick * stride + 1 + 2 * level]
		for i in bricks[brick * stride + 2 + 2 * level]:
			var voxel := data.get_voxel(first + i)
			splats.append(Vector4(voxel.position.x, voxel.position.y, voxel.position.z, voxel.half_size))
	return { "splats": _sorted(splats), "levels": levels.keys() }


func _written(count: int) -> Array:
	# Instance buffer: 3x4 row-major transform, colour, custom data per splat.
	var buffer := RenderingServer.multimesh_get_buffer(_field.get_base())
	var splats := []
	for i in count:
		var o := i * 20
		splats.append(Vector4(buffer[o + 3], buffer[o + 7], buffer[o + 11], buffer[o]))
	return _sorted(splats)


func _same(a: Array, b: Array) -> bool:
	if a.size() != b.size():
		return false
	for i in a.size():
		if (a[i] - b[i]).length() > 1e-4:
			return false
	return true


func _check_selection(data: VoxelFieldData, distance: float, orthogonal: bool) -> Array:
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL if orthogonal else Camera3D.PROJECTION_PERSPECTIVE
	_camera.size = distance
	_camera.position = Vector3(0, distance * 0.3, distance)
	_camera.look_at(Vector3(0, 2, 0))
	var count := await _count()
	var expected := _expected(data, orthogonal)
	var label := ("orthogonal size %.0f" if orthogonal else "distance %.0f") % distance
	if count != expected.splats.size():
		_failures.append("%s: %d splats on the GPU, %d by the CPU rule" % [label, count, expected.splats.size()])
	elif not _same(_written(count), expected.splats):
		_failures.append("%s: the GPU wrote other splats than the CPU rule selects" % label)
	return [count, expected.levels]


func _statistics(image: Image) -> Vector4:
	# Covered fraction and mean linear colour of covered pixels (sky is black).
	var covered := 0
	var sum := Color(0, 0, 0)
	for y in image.get_height():
		for x in image.get_width():
			var pixel := image.get_pixel(x, y)
			if pixel.get_luminance() > 0.01:
				covered += 1
				sum += pixel.srgb_to_linear()
	var n := maxf(covered, 1)
	return Vector4(float(covered) / (image.get_width() * image.get_height()), sum.r / n, sum.g / n, sum.b / n)


func _capture() -> Image:
	for frame in 12:
		await RenderingServer.frame_post_draw
	return get_viewport().get_texture().get_image()


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	_check_data()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 20, 0)
	add_child(sun)
	_camera = Camera3D.new()
	_camera.fov = 20
	_camera.far = 10000
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _run() -> void:
	var grove := _grove()
	var builder := VoxelProxyBuilder.new()
	builder.sample_spacing = 0.25
	builder.add_multimesh(grove)
	var bounds := grove.get_aabb()
	var data := builder.build_field(bounds, 64, 4)
	if data == null or data.get_level_count() != 5:
		_failures.append("grove field: %s levels" % (data.get_level_count() if data else -1))
		_finish("")
		return
	_field = VoxelField3D.new()
	_field.data = data
	_field.frustum_culling = false
	add_child(_field)

	# 1. Exact selection at four distances: farther cameras pick coarser
	# levels. Distances sit inside level bands (finest voxel 0.66 m, 2 px).
	var levels_used := {}
	var counts := []
	for distance in [300.0, 1000.0, 2000.0, 4000.0]:
		var result: Array = await _check_selection(data, distance, false)
		counts.append(result[0])
		for level in result[1]:
			levels_used[level] = true
	if levels_used.size() < 3 or not (counts[0] > counts[1] and counts[1] > counts[2] and counts[2] > counts[3]):
		_failures.append("levels %s and counts %s do not coarsen with distance" % [levels_used.keys(), counts])
	var orthogonal: Array = await _check_selection(data, 400.0, true)

	# 2. Frustum culling: fewer splats, none far outside; nothing when the
	# camera looks away.
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.position = Vector3(0, 8, 30)
	_camera.look_at(Vector3(-20, 2, 0))
	_field.frustum_culling = false
	var unculled := await _count()
	_field.frustum_culling = true
	var culled := await _count()
	var outside := 0
	for splat in _written(culled):
		for plane in _camera.get_frustum():
			if plane.distance_to(_field.to_global(Vector3(splat.x, splat.y, splat.z))) > splat.w * 1.5:
				outside += 1
				break
	_camera.look_at(Vector3(0, 8, 100))
	var away := await _count()
	if culled <= 0 or culled >= unculled or outside > 0 or away != 0:
		_failures.append("frustum: %d of %d splats kept, %d outside, %d looking away" % [culled, unculled, outside, away])

	# 3. Cap and an empty field.
	_camera.position = Vector3(0, 60, 220)
	_camera.look_at(Vector3(0, 2, 0))
	_field.max_splats = 100
	var capped := await _count()
	_field.max_splats = 0
	_field.data = null
	var empty := await _count()
	_field.data = data
	if capped != 100 or empty != 0:
		_failures.append("cap: %d splats with a 100 cap, %d without data" % [capped, empty])
	# By default the instance buffer holds exactly the most one selection can
	# produce: per brick, its largest level.
	var most := 0
	var bricks := data.get_bricks()
	for brick in data.get_brick_count():
		var largest := 0
		for level in data.get_level_count():
			largest = maxi(largest, bricks[brick * (1 + 2 * data.get_level_count()) + 2 + 2 * level])
		most += largest
	# The buffer grew on demand above (exact 15k-splat selections from a
	# 1024-splat start) and never beyond that bound.
	await _count()
	if data.get_max_splat_count() != most or _field.get_gpu_memory_usage() > most * 80 + data.get_voxels().size() + data.get_bricks().size() * 4 + 4096:
		_failures.append("capacity: %d most splats (expected %d), %d bytes" % [data.get_max_splat_count(), most, _field.get_gpu_memory_usage()])

	# 4. Distant image against the source grove.
	var source := MultiMeshInstance3D.new()
	source.multimesh = grove
	add_child(source)
	_field.visible = false
	var reference := _statistics(await _capture())
	source.visible = false
	_field.visible = true
	var image := await _capture()
	var voxel := _statistics(image)
	image.save_png(ProjectSettings.globalize_path("res://../../devtools/out/large_world").path_join("voxel_field_%s.png" % RenderingServer.get_current_rendering_driver_name()))
	if reference.x < 0.02 or absf(voxel.x / reference.x - 1.0) > 0.25:
		_failures.append("coverage %.4f against %.4f for the source" % [voxel.x, reference.x])
	if absf(voxel.y - reference.y) > 0.08 or absf(voxel.z - reference.z) > 0.08 or absf(voxel.w - reference.w) > 0.08:
		_failures.append("mean colour %s against %s" % [Vector3(voxel.y, voxel.z, voxel.w), Vector3(reference.y, reference.z, reference.w)])
	var splats := _field.get_splat_count()

	# 5. Free the field with selection work queued.
	_field.queue_free()
	_finish("%d bricks, %d voxels on 5 levels; GPU selection exact at 300-4000 m (%s splats, levels %s) and orthogonal (%d); frustum keeps %d of %d; 100-splat cap; empty field; %d splats cover %.4f vs %.4f, mean colour %s vs %s" % [data.get_brick_count(), data.get_voxel_count(), counts, levels_used.keys(), orthogonal[0], culled, unculled, splats, voxel.x, reference.x, Vector3(voxel.y, voxel.z, voxel.w), Vector3(reference.y, reference.z, reference.w)])


func _finish(summary: String) -> void:
	for frame in 4:
		await RenderingServer.frame_post_draw
	if _failures.is_empty():
		print("VOXEL_FIELD_TEST_PASS: ", summary)
	for failure in _failures:
		printerr("VOXEL_FIELD_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
