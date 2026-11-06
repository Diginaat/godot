extends Node3D
## GrassField3D: exact GPU blade counts against a CPU reference of the patch
## grid, blade heights on a triangulated non-linear height grid, density
## masking, frustum bounds, the blade budget, determinism and coverage, and
## level of detail rings: edge-dithered near and inner-radius fields that
## together place exactly the blades of one field.

const RADIUS := 10.1
const PATCH := 0.5
const BLADES := 8
var _failures: Array[String] = []
var _camera: Camera3D
var _grass: GrassField3D


func _ready() -> void:
	get_window().size = Vector2i(640, 360)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.5)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-60, 30, 0)
	add_child(sun)
	_camera = Camera3D.new()
	_camera.fov = 40
	add_child(_camera)
	_camera.current = true
	_top_view()
	_grass = GrassField3D.new()
	_grass.radius = RADIUS
	_grass.patch_size = PATCH
	_grass.blades_per_patch = BLADES
	_grass.fade_fraction = 0.0
	_grass.max_blades = 65536
	_grass.blade_width = 0.08
	add_child(_grass)
	_run.call_deferred()


func _top_view() -> void:
	# Straight down from 60 m: the whole radius is inside the frustum.
	_camera.position = Vector3(0.25, 60, 0.25)
	_camera.rotation_degrees = Vector3(-90, 0, 0)


func _expected_patches() -> int:
	# The camera sits on a patch centre, so patch centres are at 0.5 * (i, j)
	# around it; a patch is generated when its centre is inside the radius.
	var count := 0
	var half := ceili(RADIUS / PATCH)
	for j in range(-half, half + 1):
		for i in range(-half, half + 1):
			if PATCH * sqrt(float(i * i + j * j)) < RADIUS:
				count += 1
	return count


func _count(field: GrassField3D = null) -> int:
	# The count is read back asynchronously; wait until it is stable.
	if field == null:
		field = _grass
	var last := -1
	var stable := 0
	for frame in 120:
		await RenderingServer.frame_post_draw
		var count := field.get_generated_blade_count()
		stable = stable + 1 if count == last else 0
		last = count
		if stable >= 8:
			return count
	_failures.append("blade count did not settle")
	return last


func _blades(count: int, field: GrassField3D = null) -> PackedVector3Array:
	# Blade origins: translation column of each 3x4 row-major transform.
	if field == null:
		field = _grass
	var buffer := RenderingServer.multimesh_get_buffer(field.get_base())
	var origins := PackedVector3Array()
	for i in count:
		origins.append(Vector3(buffer[i * 16 + 3], buffer[i * 16 + 7], buffer[i * 16 + 11]))
	return origins


func _grid_height(heights: PackedFloat32Array, size: Vector2i, origin: Vector2, cell: float, p: Vector2) -> float:
	# The documented triangulation: diagonal from (x + 1, z) to (x, z + 1).
	var g := ((p - origin) / cell).clamp(Vector2.ZERO, Vector2(size - Vector2i.ONE) - Vector2(1e-4, 1e-4))
	var c := Vector2i(g)
	var f := g - Vector2(c)
	var a := c.y * size.x + c.x
	var v00 := heights[a]
	var v10 := heights[a + 1]
	var v01 := heights[a + size.x]
	var v11 := heights[a + size.x + 1]
	if f.x + f.y <= 1.0:
		return v00 + f.x * (v10 - v00) + f.y * (v01 - v00)
	return v11 + (1.0 - f.x) * (v01 - v11) + (1.0 - f.y) * (v10 - v11)


func _sorted(points: PackedVector3Array) -> Array:
	var list := Array(points)
	list.sort()
	return list


func _run() -> void:
	var full := _expected_patches() * BLADES
	# 1. Flat ground, full density, everything visible: exact count, stable blades.
	var count := await _count()
	if count != full:
		_failures.append("flat: %d blades, expected %d" % [count, full])
	var first := _sorted(_blades(count))
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	if _sorted(_blades(count)) != first:
		_failures.append("flat: blades differ between frames")
	# Coverage inside the projected radius, and nothing beyond it.
	var image := get_viewport().get_texture().get_image()
	var disc := RADIUS / (tan(deg_to_rad(_camera.fov * 0.5)) * 60.0) * image.get_height() * 0.5
	var inside := Vector2i.ZERO
	var beyond := 0
	for y in range(0, image.get_height(), 2):
		for x in range(0, image.get_width(), 2):
			var r := Vector2(x, y).distance_to(Vector2(image.get_width(), image.get_height()) * 0.5)
			var green := image.get_pixel(x, y).g > 0.05
			if r < disc - 4.0:
				inside += Vector2i(int(green), 1)
			elif r > disc + 6.0:
				beyond += int(green)
	var coverage := float(inside.x) / maxf(inside.y, 1)
	image.save_png(ProjectSettings.globalize_path("res://../../devtools/out/large_world").path_join("grass_field_%s.png" % RenderingServer.get_current_rendering_driver_name()))
	if coverage < 0.05 or beyond > 0:
		_failures.append("flat: grass covers %.3f inside the radius, %d px outside" % [coverage, beyond])

	# 2. Non-linear heights (x * z per point): blades stand on the triangulated grid.
	var size := Vector2i(41, 41)
	var origin := Vector2(-20, -20)
	var heights := PackedFloat32Array()
	for row in size.y:
		for column in size.x:
			heights.append(0.02 * (origin.x + column) * (origin.y + row) + 3.0)
	_grass.grid_size = size
	_grass.grid_origin = origin
	_grass.grid_cell_size = 1.0
	_grass.height_grid = heights
	count = await _count()
	var worst := 0.0
	for p in _blades(count):
		worst = maxf(worst, absf(p.y - _grid_height(heights, size, origin, 1.0, Vector2(p.x, p.z))))
	if count != full or worst > 1e-3:
		_failures.append("heights: %d blades (expected %d), worst height error %.5f" % [count, full, worst])

	# 3. Density zero for x <= 0: no blade west of x = 0, fewer blades overall.
	var density := PackedFloat32Array()
	for row in size.y:
		for column in size.x:
			density.append(1.0 if origin.x + column >= 1.0 else 0.0)
	_grass.density_grid = density
	count = await _count()
	var west := 0
	for p in _blades(count):
		west += int(p.x < 0.0)
	if west > 0 or count <= 0 or count >= full * 0.6:
		_failures.append("density: %d blades west of x = 0, %d in total (full %d)" % [west, count, full])
	_grass.density_grid = PackedFloat32Array()

	# 4. Horizontal view: frustum culling drops patches, never visible blades.
	_camera.position = Vector3(0.25, 4.0, 0.25)
	_camera.rotation_degrees = Vector3(-10, 0, 0)
	count = await _count()
	# A blade origin can lie outside by its patch's bounding sphere: half the
	# patch diagonal, the blade height and the grid's slope across the patch.
	var margin := 2.0
	var outside := 0
	for p in _blades(count):
		for plane in _camera.get_frustum():
			if plane.distance_to(p) > margin:
				outside += 1
				break
	if count <= 0 or count >= full / 2 or outside > 0:
		_failures.append("frustum: %d blades (full %d), %d outside the frustum" % [count, full, outside])
	_top_view()

	# 5. Budget: a 100-blade buffer reports 100 and stays in bounds.
	_grass.max_blades = 100
	count = await _count()
	if count != 100:
		_failures.append("budget: %d blades, expected the 100-blade cap" % count)
	if _grass.get_gpu_memory_usage() < 100 * 64:
		_failures.append("budget: memory %d B below the instance data" % _grass.get_gpu_memory_usage())

	# 6. LOD rings: a near field (outer band 4-6 m) and a ring field (inner band
	# 4-6 m, outer band like the single field) with edge dithering place
	# exactly the blades of one dithered field: same count, same origins.
	_grass.max_blades = 65536
	_grass.fade_fraction = 0.5
	_grass.edge_dither = true
	var single := await _count()
	var single_origins := _sorted(_blades(single))
	var near := GrassField3D.new()
	var ring := GrassField3D.new()
	for field in [near, ring]:
		field.patch_size = PATCH
		field.blades_per_patch = BLADES
		field.max_blades = 65536
		field.blade_width = 0.08
		field.edge_dither = true
		for property in ["grid_origin", "grid_cell_size", "grid_size", "height_grid", "density_grid"]:
			field.set(property, _grass.get(property))
	near.radius = 6.0
	near.fade_fraction = 2.0 / 6.0
	ring.radius = RADIUS
	ring.fade_fraction = 0.5
	ring.inner_radius = 6.0
	ring.inner_fade = 2.0
	_grass.visible = false
	add_child(near)
	add_child(ring)
	var near_count := await _count(near)
	var ring_count := await _count(ring)
	var union := _sorted(_blades(near_count, near) + _blades(ring_count, ring))
	if near_count == 0 or ring_count == 0 or near_count + ring_count != single or union != single_origins:
		_failures.append("rings: near %d + ring %d blades, single field %d, same origins %s" % [near_count, ring_count, single, union == single_origins])
	near.queue_free()
	ring.queue_free()

	if _failures.is_empty():
		print("GRASS_FIELD_TEST_PASS: %d blades exact on flat ground, stable across frames, %.3f covered inside the radius and none outside; heights within %.5f m on a non-linear grid; density mask; frustum; 100-blade cap; LOD rings %d + %d = %d blades at the same origins" % [full, coverage, worst, near_count, ring_count, single])
	for failure in _failures:
		printerr("GRASS_FIELD_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
