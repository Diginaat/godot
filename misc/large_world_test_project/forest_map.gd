extends Node3D
## Visual large-world smoke test: a 720 m valley built only from ordinary Godot
## nodes (MeshInstance3D, MultiMeshInstance3D, lights, Environment). The native
## GPU MultiMesh culling, cluster LOD and HZB paths are enabled; the script never
## submits draws or decides visibility itself.
##
## `night = false`: sunlit valley with sky, depth fog, SSAO, SSR water and glow.
## `night = true`: the same valley at night with volumetric fog, a lake mist
## FogVolume, about 60 local lights, emissive lanterns/windows/mushrooms/crystals,
## a campfire, a sweeping searchlight and vertex-animated fireflies. Fireflies use
## a custom vertex() shader on purpose: they must keep the conventional fallback.
##
## Run interactively, or with `-- --forest-map-smoke` for the automated check.
## `-- --view-time=T` holds the camera at route time T with every flying
## feature on (for devtools/test/gpu_intervals.ps1).

signal profile_ready(stages: Dictionary)

@export var night := false

const SIZE := 720.0
const RES := 240
const PATCH := 48.0
const WATER := -3.0
const LAKE := Vector2(135, -70)
const VILLAGE := Vector2(26, 58)
const SEGMENT_TIME := 9.0
const STAGES: Array[String] = ["MultiMesh Frustum Culling", "Mesh CLOD Selection", "Mesh CLOD HZB"]
# Far field: beyond FAR_DISTANCE a patch's trees and bushes become one voxel
# splat proxy (VoxelProxyBuilder) with FAR_RESOLUTION voxels across the patch.
const FAR_DISTANCE := 180.0
const FAR_OVERLAP := 4.0 # Half-width of the cross-fade band (or of the hard-cut overlap).
const FAR_RESOLUTION := 24
# GPU far field (default when flying; the bench opts in with --far-gpu, and
# --far-static restores the static proxies): a VoxelField3D per patch with
# FAR_GPU_RESOLUTION voxels across the patch on its finest level (0.75 m) and
# 4 bricks per axis; the GPU picks per brick the coarsest level whose voxels
# stay under FAR_GPU_PIXELS, so voxels shrink with distance in metres and stay
# about 2 px on screen.
const FAR_GPU_RESOLUTION := 64
const FAR_GPU_PIXELS := 2.0

var _hills := FastNoiseLite.new()
var _ridges := FastNoiseLite.new()
var _detail := FastNoiseLite.new()
var _density := FastNoiseLite.new()
var _heights := PackedFloat32Array()
var _normals := PackedVector3Array()
var _village_height := 0.0
var _random := RandomNumberGenerator.new()

var _route: Array[Vector3] = []
var _route_time := 0.0
var _autopilot := true
# --view-time=T: hold the camera at route time T (seconds at the default
# speed), for profiling one view with every flying feature on.
var _view_time := -1.0
var _speed := 22.0
var _smoke := false
var _bench := false
var _mesh_lods := false
var _far_field := false
var _thinning := false
var _gpu_grass := false
var _far_hard_cut := false
var _far_gpu := false
var _far_compare := false
var _far_gpu_resolution := FAR_GPU_RESOLUTION
var _far_gpu_pixels := FAR_GPU_PIXELS
var _grass_field: GrassField3D
var _far_builder := VoxelProxyBuilder.new()
var _far_patches: Array[Dictionary] = [] # {"proxy": MeshInstance3D, "sources": Array[MultiMeshInstance3D]}
var _gpu_geometry := true
var _failures: Array[String] = []

var _camera: Camera3D
var _environment: Environment
var _hud: Label
var _hud_panel: PanelContainer
var _hud_timer := 0.0
var _stage_state := {}
var _local_lights: Array[Light3D] = []
var _campfire: OmniLight3D
var _searchlight: SpotLight3D
var _counts := {"trees": 0, "bushes": 0, "grass": 0, "flowers": 0, "rocks": 0, "mushrooms": 0, "crystals": 0, "fireflies": 0, "patches": 0, "far_proxies": 0, "far_voxels": 0}
var _terrain_triangles := 0


class Batch:
	var data := PackedFloat32Array()
	var count := 0

	func add(position: Vector3, angle: float, scale: float, color: Color, tilt := Vector2.ZERO) -> void:
		var basis := Basis.from_euler(Vector3(tilt.x, angle, tilt.y)) * Basis.from_scale(Vector3.ONE * scale)
		# MultiMesh TRANSFORM_3D rows, followed by the instance color.
		data.append_array(PackedFloat32Array([
				basis.x.x, basis.y.x, basis.z.x, position.x,
				basis.x.y, basis.y.y, basis.z.y, position.y,
				basis.x.z, basis.y.z, basis.z.z, position.z,
				color.r, color.g, color.b, color.a]))
		count += 1


class Builder:
	## Merges primitive meshes into one vertex-colored surface. A vertical
	## gradient per primitive fakes ambient occlusion without textures.
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()

	func add(primitive: PrimitiveMesh, transform: Transform3D, color: Color, shade_low := 1.0) -> void:
		var arrays := primitive.get_mesh_arrays()
		var source_vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var source_normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var source_indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var low := INF
		var high := -INF
		for vertex in source_vertices:
			low = minf(low, vertex.y)
			high = maxf(high, vertex.y)
		var normal_basis := transform.basis.inverse().transposed()
		var base := vertices.size()
		for k in source_vertices.size():
			vertices.append(transform * source_vertices[k])
			normals.append((normal_basis * source_normals[k]).normalized())
			var t := 1.0 if high <= low else (source_vertices[k].y - low) / (high - low)
			var shade := lerpf(shade_low, 1.0, t)
			colors.append(Color(color.r * shade, color.g * shade, color.b * shade))
		for index in source_indices:
			indices.append(base + index)

	func build(material: Material, lods := false) -> ArrayMesh:
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = vertices
		arrays[Mesh.ARRAY_NORMAL] = normals
		arrays[Mesh.ARRAY_COLOR] = colors
		arrays[Mesh.ARRAY_INDEX] = indices
		if lods:
			# Godot's own index LODs, as the scene importer generates them.
			var importer := ImporterMesh.new()
			importer.add_surface(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, material)
			importer.generate_lods(25.0, 60.0, [])
			return importer.get_mesh()
		var mesh := ArrayMesh.new()
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(0, material)
		return mesh


# --- Terrain ------------------------------------------------------------------

func _road_x(z: float) -> float:
	return -38.0 + 52.0 * sin(z * 0.009) + 12.0 * sin(z * 0.023)


func _raw_height(x: float, z: float) -> float:
	var h := 7.0 + 17.0 * _hills.get_noise_2d(x, z) + 1.2 * _detail.get_noise_2d(x, z)
	var rim := smoothstep(185.0, 330.0, maxf(absf(x), absf(z)))
	h += rim * (38.0 + 60.0 * (0.5 + 0.5 * _ridges.get_noise_2d(x, z)))
	h -= 17.0 * (1.0 - smoothstep(25.0, 105.0, Vector2(x, z).distance_to(LAKE)))
	return h


func _height(x: float, z: float) -> float:
	var h := lerpf(_village_height, _raw_height(x, z), smoothstep(30.0, 52.0, Vector2(x, z).distance_to(VILLAGE)))
	# The road follows the terrain but rides on a causeway where it crosses water.
	return lerpf(maxf(h, WATER + 1.4), h, smoothstep(3.5, 9.0, absf(x - _road_x(z))))


func _ground(x: float, z: float) -> float:
	var gx := clampf((x / SIZE + 0.5) * RES, 0.0, RES - 0.001)
	var gz := clampf((z / SIZE + 0.5) * RES, 0.0, RES - 0.001)
	var ix := int(gx)
	var iz := int(gz)
	var fx := gx - ix
	var fz := gz - iz
	var w := RES + 1
	var top := lerpf(_heights[iz * w + ix], _heights[iz * w + ix + 1], fx)
	var bottom := lerpf(_heights[(iz + 1) * w + ix], _heights[(iz + 1) * w + ix + 1], fx)
	return lerpf(top, bottom, fz)


func _flatness(x: float, z: float) -> float:
	var ix := clampi(roundi((x / SIZE + 0.5) * RES), 0, RES)
	var iz := clampi(roundi((z / SIZE + 0.5) * RES), 0, RES)
	return _normals[iz * (RES + 1) + ix].y


func _forest(x: float, z: float) -> float:
	return smoothstep(-0.18, 0.22, _density.get_noise_2d(x, z))


func _terrain_mesh() -> ArrayMesh:
	var w := RES + 1
	var step := SIZE / RES
	_heights.resize(w * w)
	_normals.resize(w * w)
	for row in w:
		for column in w:
			_heights[row * w + column] = _height((float(column) / RES - 0.5) * SIZE, (float(row) / RES - 0.5) * SIZE)
	var vertices := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for row in w:
		for column in w:
			var x := (float(column) / RES - 0.5) * SIZE
			var z := (float(row) / RES - 0.5) * SIZE
			var h := _heights[row * w + column]
			var dx := _heights[row * w + mini(column + 1, RES)] - _heights[row * w + maxi(column - 1, 0)]
			var dz := _heights[mini(row + 1, RES) * w + column] - _heights[maxi(row - 1, 0) * w + column]
			var normal := Vector3(-dx, 2.0 * step, -dz).normalized()
			_normals[row * w + column] = normal
			vertices.append(Vector3(x, h, z))
			var meadow := Color(0.13, 0.27, 0.06).lerp(Color(0.30, 0.40, 0.10), 0.5 + 0.5 * _detail.get_noise_2d(x * 2.5, z * 2.5))
			var color := meadow.lerp(Color(0.08, 0.13, 0.05), _forest(x, z) * 0.75)
			color = color.lerp(Color(0.40, 0.35, 0.24), smoothstep(WATER + 1.8, WATER + 0.5, h))
			if h < WATER:
				color = color.lerp(Color(0.18, 0.22, 0.17), 0.6)
			color = color.lerp(Color(0.27, 0.25, 0.22), smoothstep(0.88, 0.72, normal.y))
			color = color.lerp(Color(0.93, 0.95, 0.99), smoothstep(74.0, 88.0, h) * smoothstep(0.6, 0.8, normal.y))
			color = color.lerp(Color(0.38, 0.29, 0.19), smoothstep(4.2, 2.6, absf(x - _road_x(z))))
			color = color.lerp(Color(0.36, 0.28, 0.19), smoothstep(15.0, 11.0, Vector2(x, z).distance_to(VILLAGE)))
			colors.append(color)
	for row in RES:
		for column in RES:
			var a := row * w + column
			indices.append_array(PackedInt32Array([a, a + 1, a + w, a + 1, a + w + 1, a + w]))
	_terrain_triangles = indices.size() / 3
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = _normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, _vertex_color_material(0.95))
	return mesh


func _add_grass_field() -> void:
	# GPU blades on the terrain's own height grid (same triangulation), dense
	# in open meadows and absent under forest, on rock, water, road and snow.
	var w := RES + 1
	var density := PackedFloat32Array()
	density.resize(w * w)
	for row in w:
		for column in w:
			var x := (float(column) / RES - 0.5) * SIZE
			var z := (float(row) / RES - 0.5) * SIZE
			var h := _heights[row * w + column]
			var d := 1.0 - 0.9 * _forest(x, z)
			d *= smoothstep(0.82, 0.93, _normals[row * w + column].y)
			d *= smoothstep(WATER + 0.8, WATER + 1.8, h) * (1.0 - smoothstep(66.0, 76.0, h))
			d *= smoothstep(3.5, 5.0, absf(x - _road_x(z))) * smoothstep(13.0, 17.0, Vector2(x, z).distance_to(VILLAGE))
			density[row * w + column] = d
	_grass_field = GrassField3D.new()
	_grass_field.radius = 45.0
	_grass_field.patch_size = 0.5
	_grass_field.blades_per_patch = 24
	_grass_field.blade_height_min = 0.22
	_grass_field.blade_height_max = 0.5
	_grass_field.blade_width = 0.05
	_grass_field.color_a = Color(0.20, 0.36, 0.08)
	_grass_field.color_b = Color(0.42, 0.52, 0.15)
	_grass_field.grid_size = Vector2i(w, w)
	_grass_field.grid_origin = Vector2(-0.5 * SIZE, -0.5 * SIZE)
	_grass_field.grid_cell_size = SIZE / RES
	_grass_field.height_grid = _heights
	_grass_field.density_grid = density
	_grass_field.visible = _gpu_grass
	add_child(_grass_field)


# --- Meshes and materials -----------------------------------------------------

func _material(color: Color, roughness := 0.9) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = roughness
	return material


func _vertex_color_material(roughness := 0.9) -> StandardMaterial3D:
	var material := _material(Color.WHITE, roughness)
	material.vertex_color_use_as_albedo = true
	return material


func _emissive(color: Color, energy: float) -> StandardMaterial3D:
	var material := _material(color.darkened(0.3), 0.6)
	material.emission_enabled = true
	material.emission = color
	material.emission_energy_multiplier = energy
	return material


func _cone(bottom: float, top: float, height: float, segments: int) -> CylinderMesh:
	var mesh := CylinderMesh.new()
	mesh.bottom_radius = bottom
	mesh.top_radius = top
	mesh.height = height
	mesh.radial_segments = segments
	mesh.rings = 1
	mesh.cap_top = top > 0.0
	return mesh


func _sphere(radius: float, segments: int, rings: int) -> SphereMesh:
	var mesh := SphereMesh.new()
	mesh.radius = radius
	mesh.height = radius * 2.0
	mesh.radial_segments = segments
	mesh.rings = rings
	return mesh


func _at(position: Vector3, angle := 0.0, scale := Vector3.ONE, tilt := Vector2.ZERO) -> Transform3D:
	return Transform3D(Basis.from_euler(Vector3(tilt.x, angle, tilt.y)) * Basis.from_scale(scale), position)


func _conifer(tiers: int, width: float, tier_height: float, color: Color) -> ArrayMesh:
	var builder := Builder.new()
	builder.add(_cone(0.22 * width, 0.12 * width, 2.2, 6), _at(Vector3(0, 1.1, 0)), Color(0.30, 0.20, 0.12), 0.6)
	for tier in tiers:
		var t := float(tier) / tiers
		var radius := lerpf(1.7, 0.45, t) * width
		var height := lerpf(2.5, 1.5, t) * tier_height
		var base := 1.3 + tier * 1.15 * tier_height
		builder.add(_cone(radius, 0.0, height, 9), _at(Vector3(0, base + height * 0.5, 0), tier * 0.7), color.lightened(0.12 * t), 0.42)
	return builder.build(_vertex_color_material(), _mesh_lods)


func _broadleaf() -> ArrayMesh:
	var builder := Builder.new()
	builder.add(_cone(0.28, 0.16, 3.4, 7), _at(Vector3(0, 1.7, 0)), Color(0.33, 0.24, 0.16), 0.6)
	var crown := Color(0.30, 0.46, 0.09)
	var lobes := [Vector4(0, 4.9, 0, 2.1), Vector4(1.3, 4.2, 0.4, 1.5), Vector4(-1.2, 4.4, -0.5, 1.6), Vector4(0.3, 4.1, -1.3, 1.4), Vector4(-0.4, 5.9, 0.6, 1.4)]
	for lobe: Vector4 in lobes:
		builder.add(_sphere(lobe.w, 10, 6), _at(Vector3(lobe.x, lobe.y, lobe.z), lobe.x, Vector3(1, 0.85, 1)), crown, 0.32)
	return builder.build(_vertex_color_material(), _mesh_lods)


func _bush() -> ArrayMesh:
	var builder := Builder.new()
	for lobe: Vector4 in [Vector4(0, 0.55, 0, 0.8), Vector4(0.6, 0.4, 0.3, 0.6), Vector4(-0.5, 0.45, -0.3, 0.65)]:
		builder.add(_sphere(lobe.w, 8, 5), _at(Vector3(lobe.x, lobe.y, lobe.z), 0.0, Vector3(1, 0.8, 1)), Color(0.14, 0.29, 0.07), 0.4)
	return builder.build(_vertex_color_material(), _mesh_lods)


func _grass_tuft() -> ArrayMesh:
	var builder := Builder.new()
	var random := RandomNumberGenerator.new()
	random.seed = 4242
	for blade in 9:
		var height := random.randf_range(0.45, 0.95)
		var offset := Vector3(random.randf_range(-0.3, 0.3), height * 0.5, random.randf_range(-0.3, 0.3))
		var tilt := Vector2(random.randf_range(-0.35, 0.35), random.randf_range(-0.35, 0.35))
		builder.add(_cone(0.045, 0.0, height, 3), _at(offset, random.randf_range(0, TAU), Vector3.ONE, tilt), Color(0.26, 0.45, 0.10), 0.3)
	return builder.build(_vertex_color_material(), _mesh_lods)


func _flower() -> ArrayMesh:
	var builder := Builder.new()
	builder.add(_cone(0.015, 0.01, 0.5, 3), _at(Vector3(0, 0.25, 0)), Color(0.3, 0.5, 0.2), 0.6)
	builder.add(_sphere(0.09, 6, 3), _at(Vector3(0, 0.52, 0), 0.0, Vector3(1, 0.5, 1)), Color.WHITE, 0.8)
	return builder.build(_vertex_color_material(0.7))


func _mushroom() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var stem_arrays := _cone(0.05, 0.035, 0.22, 5).get_mesh_arrays()
	var cap_arrays := _sphere(0.13, 8, 4).get_mesh_arrays()
	var stem_vertices: PackedVector3Array = stem_arrays[Mesh.ARRAY_VERTEX]
	for k in stem_vertices.size():
		stem_vertices[k].y += 0.11
	stem_arrays[Mesh.ARRAY_VERTEX] = stem_vertices
	var cap_vertices: PackedVector3Array = cap_arrays[Mesh.ARRAY_VERTEX]
	for k in cap_vertices.size():
		cap_vertices[k] = Vector3(cap_vertices[k].x, maxf(cap_vertices[k].y, 0.0) * 0.6 + 0.22, cap_vertices[k].z)
	cap_arrays[Mesh.ARRAY_VERTEX] = cap_vertices
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, stem_arrays)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, cap_arrays)
	mesh.surface_set_material(0, _material(Color(0.85, 0.85, 0.78)))
	mesh.surface_set_material(1, _emissive(Color(0.15, 0.95, 0.85), 9.0))
	return mesh


func _rock_mesh(seed: int) -> ArrayMesh:
	var noise := FastNoiseLite.new()
	noise.seed = seed
	noise.frequency = 1.3
	noise.fractal_octaves = 3
	var arrays := _sphere(1.0, 28, 14).get_mesh_arrays()
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for k in vertices.size():
		var p := vertices[k] * (1.0 + 0.34 * noise.get_noise_3dv(vertices[k]))
		p.y *= 0.72
		if p.y < -0.3:
			p.y = -0.3 + (p.y + 0.3) * 0.25
		vertices[k] = p
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = null
	arrays[Mesh.ARRAY_TANGENT] = null
	var tool := SurfaceTool.new()
	tool.create_from_arrays(arrays)
	tool.generate_normals()
	var mesh := tool.commit()
	if mesh.generate_cluster_lods() != OK:
		_failures.append("rock %d cluster LOD generation failed" % seed)
	return mesh


# --- World population -----------------------------------------------------------

func _spawn(batch: Batch, mesh: Mesh, origin: Vector3, shadows := true, visibility_end := 0.0) -> MultiMeshInstance3D:
	if batch.count == 0:
		return null
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = mesh
	multimesh.instance_count = batch.count
	multimesh.buffer = batch.data
	var node := MultiMeshInstance3D.new()
	node.multimesh = multimesh
	node.position = origin
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadows else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.visibility_range_end = visibility_end
	add_child(node)
	_counts.patches += 1
	return node


func _thin(node: MultiMeshInstance3D, begin: float, end: float) -> void:
	if node and _thinning:
		node.multimesh.distance_thinning_begin = begin
		node.multimesh.distance_thinning_end = end


func _add_far_proxy(origin: Vector3, sources: Array[MultiMeshInstance3D]) -> void:
	_far_builder.clear()
	var bounds := AABB()
	for node in sources:
		_far_builder.add_multimesh(node.multimesh)
		bounds = node.multimesh.get_aabb() if bounds.size == Vector3.ZERO else bounds.merge(node.multimesh.get_aabb())
	var proxy: GeometryInstance3D
	var voxels := 0
	if _far_gpu:
		var data := _far_builder.build_field(bounds, _far_gpu_resolution, 4)
		if data == null:
			return
		var field := VoxelField3D.new()
		field.data = data
		field.voxel_pixels = _far_gpu_pixels
		proxy = field
		voxels = data.get_voxel_count()
	else:
		var mesh := _far_builder.build(bounds, FAR_RESOLUTION)
		if mesh == null:
			return
		var instance := MeshInstance3D.new()
		instance.mesh = mesh
		proxy = instance
		voxels = mesh.surface_get_array_len(0) / 4
	proxy.position = origin
	# Far patches lie beyond the directional shadow distance.
	proxy.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(proxy)
	_far_patches.append({"proxy": proxy, "sources": sources})
	_counts.far_proxies += 1
	_counts.far_voxels += voxels
	_apply_far_field(_far_patches.back())


func _apply_far_field(patch: Dictionary) -> void:
	# Proxy and sources share the patch centre, so their ranges switch together.
	# Cross-fade: Godot fades the sources out and the proxy in over the same
	# band (smoothstep alphas that sum to one); only patches inside the band
	# draw in the alpha pass. Hard cut: both draw opaque inside the overlap.
	var fade := GeometryInstance3D.VISIBILITY_RANGE_FADE_DISABLED if _far_hard_cut else GeometryInstance3D.VISIBILITY_RANGE_FADE_SELF
	var margin := 0.0 if _far_hard_cut else FAR_OVERLAP
	patch.proxy.visible = _far_field
	patch.proxy.visibility_range_begin = FAR_DISTANCE - FAR_OVERLAP if _far_hard_cut else FAR_DISTANCE
	patch.proxy.visibility_range_begin_margin = margin
	patch.proxy.visibility_range_fade_mode = fade
	for node: MultiMeshInstance3D in patch.sources:
		node.visibility_range_end = (FAR_DISTANCE + FAR_OVERLAP if _far_hard_cut else FAR_DISTANCE) if _far_field else 0.0
		node.visibility_range_end_margin = margin if _far_field else 0.0
		node.visibility_range_fade_mode = fade if _far_field else GeometryInstance3D.VISIBILITY_RANGE_FADE_DISABLED


func _populate() -> void:
	var pine := _conifer(4, 1.0, 1.0, Color(0.07, 0.24, 0.07))
	var fir := _conifer(6, 0.75, 1.15, Color(0.04, 0.16, 0.08))
	var broadleaf := _broadleaf()
	var bush := _bush()
	var grass := _grass_tuft()
	var flower := _flower()
	var mushroom := _mushroom() if night else null
	var flower_colors := [Color(1.0, 0.85, 0.2), Color(0.75, 0.45, 1.0), Color(1, 1, 1), Color(1.0, 0.35, 0.3)]
	var half := int(SIZE / PATCH / 2)
	for patch_z in range(-half, half):
		for patch_x in range(-half, half):
			var origin := Vector3((patch_x + 0.5) * PATCH, 0, (patch_z + 0.5) * PATCH)
			var batches := {"pine": Batch.new(), "fir": Batch.new(), "broadleaf": Batch.new(), "bush": Batch.new(), "grass": Batch.new(), "flower": Batch.new(), "mushroom": Batch.new()}
			for row in 23:
				for column in 23:
					var x := origin.x + (column - 11.0) * 2.08 + _random.randf_range(-0.8, 0.8)
					var z := origin.z + (row - 11.0) * 2.08 + _random.randf_range(-0.8, 0.8)
					var h := _ground(x, z)
					var forest := _forest(x, z)
					if h < WATER + 1.2 or h > 52.0 or absf(x - _road_x(z)) < 6.0 or Vector2(x, z).distance_to(VILLAGE) < 34.0 or _flatness(x, z) < 0.8:
						continue
					var local := Vector3(x - origin.x, h - 0.25, z - origin.z)
					var angle := _random.randf_range(0, TAU)
					if _random.randf() < forest:
						var scale := _random.randf_range(0.75, 1.5)
						var tint := Color(_random.randf_range(0.75, 1.0), _random.randf_range(0.8, 1.0), _random.randf_range(0.7, 1.0))
						var kind := "fir" if h > 26.0 + _random.randf_range(-6, 6) else ("broadleaf" if h < 12.0 and _random.randf() < 0.65 else "pine")
						if kind == "broadleaf" and _random.randf() < 0.22:
							tint = Color(1.0, _random.randf_range(0.45, 0.62), 0.2) # Autumn crowns.
						batches[kind].add(local, angle, scale, tint)
						if night and forest > 0.3 and forest < 0.75 and _random.randf() < 0.12:
							for m in _random.randi_range(2, 5):
								var spot := Vector3(x + _random.randf_range(-1.2, 1.2), 0, z + _random.randf_range(-1.2, 1.2))
								batches.mushroom.add(Vector3(spot.x - origin.x, _ground(spot.x, spot.z) - 0.02, spot.z - origin.z), _random.randf_range(0, TAU), _random.randf_range(0.7, 1.6), Color.WHITE)
					elif _random.randf() < 0.1:
						batches.bush.add(local, angle, _random.randf_range(0.7, 1.4), Color(_random.randf_range(0.8, 1.0), _random.randf_range(0.85, 1.0), 0.8))
			for row in 30:
				for column in 30:
					var x := origin.x + (column - 14.5) * 1.6 + _random.randf_range(-0.6, 0.6)
					var z := origin.z + (row - 14.5) * 1.6 + _random.randf_range(-0.6, 0.6)
					var h := _ground(x, z)
					if h < WATER + 0.8 or h > 45.0 or absf(x - _road_x(z)) < 3.6 or Vector2(x, z).distance_to(VILLAGE) < 12.0 or _flatness(x, z) < 0.86:
						continue
					var meadow := 1.0 - _forest(x, z)
					var local := Vector3(x - origin.x, h - 0.05, z - origin.z)
					if _random.randf() < 0.75 * meadow + 0.1:
						var lush := _random.randf_range(0.75, 1.0)
						batches.grass.add(local, _random.randf_range(0, TAU), _random.randf_range(0.7, 1.35), Color(lush, lush, _random.randf_range(0.7, 1.0)))
					if _random.randf() < 0.07 * meadow:
						batches.flower.add(local, _random.randf_range(0, TAU), _random.randf_range(0.8, 1.3), flower_colors[_random.randi() % flower_colors.size()])
			var sources: Array[MultiMeshInstance3D] = []
			for kind in ["pine", "fir", "broadleaf", "bush"]:
				var node := _spawn(batches[kind], {"pine": pine, "fir": fir, "broadleaf": broadleaf, "bush": bush}[kind], origin)
				if node:
					sources.append(node)
			if not sources.is_empty():
				_add_far_proxy(origin, sources)
			# Ground cover thins out with distance (MultiMesh distance thinning) and
			# is gone at its visibility range end.
			_thin(_spawn(batches.grass, grass, origin, false, 130.0), 40.0, 130.0)
			_thin(_spawn(batches.flower, flower, origin, false, 85.0), 30.0, 85.0)
			if night:
				_thin(_spawn(batches.mushroom, mushroom, origin, false, 220.0), 80.0, 220.0)
			_counts.trees += batches.pine.count + batches.fir.count + batches.broadleaf.count
			_counts.bushes += batches.bush.count
			_counts.grass += batches.grass.count
			_counts.flowers += batches.flower.count
			_counts.mushrooms += batches.mushroom.count


func _place_rocks() -> void:
	var meshes: Array[ArrayMesh] = [_rock_mesh(3), _rock_mesh(17), _rock_mesh(29)]
	var crystals := Batch.new()
	var spots: Array[Vector4] = [] # x, unused, z, scale
	for i in 90: # Boulders lining the road.
		var z := -300.0 + i * 6.8
		var side := -1.0 if i % 2 == 0 else 1.0
		spots.append(Vector4(_road_x(z) + side * _random.randf_range(5.5, 8.0), 0, z, _random.randf_range(0.6, 1.8)))
	for i in 40: # Lake shore.
		var angle := TAU * i / 40.0
		var radius := _random.randf_range(70.0, 95.0)
		spots.append(Vector4(LAKE.x + cos(angle) * radius, 0, LAKE.y + sin(angle) * radius, _random.randf_range(1.0, 3.0)))
	var attempts := 0
	while spots.size() < 280 and attempts < 4000: # Cliffs and mountain boulders.
		attempts += 1
		var x := _random.randf_range(-330, 330)
		var z := _random.randf_range(-330, 330)
		if _flatness(x, z) < 0.86 and _ground(x, z) > WATER + 1.0:
			spots.append(Vector4(x, 0, z, _random.randf_range(2.0, 6.0)))
	for i in spots.size():
		var spot := spots[i]
		var rock := MeshInstance3D.new()
		rock.mesh = meshes[i % meshes.size()]
		rock.position = Vector3(spot.x, _ground(spot.x, spot.z) - (0.1 + 0.45 * (1.0 - _flatness(spot.x, spot.z)) * 3.0) * spot.w, spot.z)
		rock.rotation = Vector3(_random.randf_range(-0.2, 0.2), _random.randf_range(0, TAU), _random.randf_range(-0.2, 0.2))
		rock.scale = Vector3(_random.randf_range(0.8, 1.4), _random.randf_range(0.7, 1.2), _random.randf_range(0.8, 1.3)) * spot.w
		# A distinct material per rock avoids Godot's automatic repeat batching,
		# which resident CLOD does not support yet.
		var tone := _random.randf_range(0.32, 0.55)
		rock.material_override = _material(Color(tone, tone * 0.95, tone * 0.88), 0.95)
		add_child(rock)
		_counts.rocks += 1
		if night and i % 5 == 0:
			for c in _random.randi_range(3, 6):
				var at := rock.position + Vector3(_random.randf_range(-1.4, 1.4), 0, _random.randf_range(-1.4, 1.4)) * spot.w * 0.7
				crystals.add(Vector3(at.x, _ground(at.x, at.z), at.z), _random.randf_range(0, TAU), _random.randf_range(0.6, 1.5) * clampf(spot.w * 0.5, 0.7, 2.0), Color.WHITE, Vector2(_random.randf_range(-0.45, 0.45), _random.randf_range(-0.45, 0.45)))
	if night:
		var arrays := _cone(0.26, 0.0, 1.8, 6).get_mesh_arrays()
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		for k in vertices.size():
			vertices[k].y += 0.8
		arrays[Mesh.ARRAY_VERTEX] = vertices
		var mesh := ArrayMesh.new()
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(0, _emissive(Color(0.95, 0.25, 1.0), 6.0))
		_spawn(crystals, mesh, Vector3.ZERO, false)
		_counts.crystals = crystals.count


func _water() -> void:
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
uniform vec3 deep_color : source_color = vec3(0.005, 0.04, 0.06);
void fragment() {
	vec2 p = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xz;
	vec3 n = normalize(vec3(
			0.05 * sin(p.x * 0.35 + TIME * 1.1) + 0.03 * sin(p.y * 0.9 - TIME * 1.7),
			1.0,
			0.05 * cos(p.y * 0.42 + TIME * 0.9) + 0.03 * cos(p.x * 1.1 + TIME * 1.3)));
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);
	ALBEDO = deep_color;
	ROUGHNESS = 0.03;
	SPECULAR = 0.7;
}
"""
	var material := ShaderMaterial.new()
	material.shader = shader
	var plane := PlaneMesh.new()
	plane.size = Vector2(SIZE, SIZE)
	plane.material = material
	var water := MeshInstance3D.new()
	water.mesh = plane
	water.position.y = WATER
	water.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(water)


# --- Night props ------------------------------------------------------------------

func _light(position: Vector3, color: Color, energy: float, range: float, shadows := false) -> OmniLight3D:
	var light := OmniLight3D.new()
	light.position = position
	light.light_color = color
	light.light_energy = energy
	light.omni_range = range
	light.shadow_enabled = shadows
	light.light_volumetric_fog_energy = 1.6
	add_child(light)
	_local_lights.append(light)
	return light


func _night_props() -> void:
	var warm := Color(1.0, 0.62, 0.3)
	var posts := Batch.new()
	var heads := Batch.new()
	var index := 0
	for z in range(-290, 300, 10):
		var side := -1.0 if index % 2 == 0 else 1.0
		index += 1
		var x := _road_x(z) + side * 4.8
		var ground := _ground(x, z)
		if ground < WATER + 0.5:
			continue
		posts.add(Vector3(x, ground, z), 0.0, 1.0, Color.WHITE)
		heads.add(Vector3(x, ground + 3.3, z), 0.0, 1.0, Color.WHITE)
		_light(Vector3(x, ground + 3.2, z), warm, 2.6, 13.0)
	var post_arrays := _cone(0.07, 0.05, 3.2, 6).get_mesh_arrays()
	var post_vertices: PackedVector3Array = post_arrays[Mesh.ARRAY_VERTEX]
	for k in post_vertices.size():
		post_vertices[k].y += 1.6
	post_arrays[Mesh.ARRAY_VERTEX] = post_vertices
	var posts_mesh := ArrayMesh.new()
	posts_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, post_arrays)
	posts_mesh.surface_set_material(0, _material(Color(0.12, 0.09, 0.07)))
	_spawn(posts, posts_mesh, Vector3.ZERO)
	var head_mesh := _sphere(0.2, 10, 6)
	head_mesh.material = _emissive(warm, 7.0)
	_spawn(heads, head_mesh, Vector3.ZERO, false)

	var center := Vector3(VILLAGE.x, _village_height, VILLAGE.y)
	var wood := _material(Color(0.42, 0.28, 0.17))
	var roof := _material(Color(0.30, 0.11, 0.08))
	var window := _emissive(Color(1.0, 0.72, 0.36), 5.0)
	var body := BoxMesh.new()
	body.size = Vector3(6, 3, 5)
	var roof_mesh := PrismMesh.new()
	roof_mesh.size = Vector3(6.8, 2.1, 5.8)
	var window_mesh := BoxMesh.new()
	window_mesh.size = Vector3(1.0, 0.8, 0.08)
	for i in 7:
		var angle := TAU * i / 7.0 + 0.3
		var position := center + Vector3(cos(angle), 0, sin(angle)) * 21.0
		var cabin := Node3D.new()
		cabin.position = position
		# -Z away from the plaza, so windows on +Z face the campfire.
		cabin.basis = Basis.looking_at(Vector3(position.x - center.x, 0, position.z - center.z))
		add_child(cabin)
		for part in [[body, wood, Vector3(0, 1.5, 0)], [roof_mesh, roof, Vector3(0, 4.05, 0)], [window_mesh, window, Vector3(-1.5, 1.7, 2.52)], [window_mesh, window, Vector3(1.5, 1.7, 2.52)]]:
			var node := MeshInstance3D.new()
			node.mesh = part[0]
			node.material_override = part[1]
			node.position = part[2]
			cabin.add_child(node)
		var light := _light(Vector3.ZERO, Color(1.0, 0.7, 0.38), 1.8, 10.0, i < 2)
		light.position = cabin.to_global(Vector3(0, 1.8, 3.6))

	var logs := _material(Color(0.2, 0.13, 0.08))
	for i in 5:
		var log := MeshInstance3D.new()
		log.mesh = _cone(0.13, 0.13, 1.6, 7)
		log.material_override = logs
		log.position = center + Vector3(0, 0.15, 0)
		log.rotation = Vector3(PI * 0.5, TAU * i / 5.0, 0)
		add_child(log)
	var flame_material := _emissive(Color(1.0, 0.45, 0.1), 12.0)
	for i in 3:
		var flame := MeshInstance3D.new()
		flame.mesh = _cone(0.45 - i * 0.1, 0.0, 1.3 + i * 0.35, 8)
		flame.material_override = flame_material
		flame.position = center + Vector3(0.12 * (i - 1), 0.75 + i * 0.12, 0.08 * i)
		flame.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(flame)
	_campfire = _light(center + Vector3(0, 1.6, 0), Color(1.0, 0.52, 0.18), 5.0, 26.0, true)

	var tower_base := center + Vector3(-34, 0, -30)
	tower_base.y = _ground(tower_base.x, tower_base.z)
	var timber := _material(Color(0.25, 0.17, 0.11))
	for corner in [Vector3(-1.6, 0, -1.6), Vector3(1.6, 0, -1.6), Vector3(-1.6, 0, 1.6), Vector3(1.6, 0, 1.6)]:
		var leg := MeshInstance3D.new()
		leg.mesh = _cone(0.18, 0.15, 13.0, 6)
		leg.material_override = timber
		leg.position = tower_base + corner + Vector3(0, 6.5, 0)
		add_child(leg)
	var platform := MeshInstance3D.new()
	var platform_mesh := BoxMesh.new()
	platform_mesh.size = Vector3(4.4, 0.4, 4.4)
	platform.mesh = platform_mesh
	platform.material_override = timber
	platform.position = tower_base + Vector3(0, 13.2, 0)
	add_child(platform)
	_searchlight = SpotLight3D.new()
	_searchlight.position = tower_base + Vector3(0, 14.6, 0)
	_searchlight.light_color = Color(0.85, 0.92, 1.0)
	_searchlight.light_energy = 60.0
	_searchlight.spot_range = 220.0
	_searchlight.spot_angle = 12.0
	_searchlight.shadow_enabled = true
	_searchlight.light_volumetric_fog_energy = 9.0
	add_child(_searchlight)
	_local_lights.append(_searchlight)

	var fireflies := Batch.new()
	while fireflies.count < 2500:
		var x := _random.randf_range(-260, 260)
		var z := _random.randf_range(-260, 260)
		var ground := _ground(x, z)
		if ground > WATER + 0.5 and _forest(x, z) < 0.55:
			fireflies.add(Vector3(x, ground + _random.randf_range(0.6, 3.5), z), 0.0, 1.0, Color.WHITE)
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode unshaded, shadows_disabled;
uniform vec3 glow : source_color = vec3(0.75, 1.0, 0.3);
varying float pulse;
void vertex() {
	// Deliberate custom vertex motion: GPU culling must fall back for this MultiMesh.
	float id = float(INSTANCE_ID);
	VERTEX += vec3(sin(TIME * 0.7 + id * 1.37), 0.5 * sin(TIME * 1.1 + id * 2.11), cos(TIME * 0.6 + id * 0.73)) * 1.3;
	pulse = 0.35 + 0.65 * max(sin(TIME * 2.6 + id * 5.1), 0.0);
}
void fragment() {
	ALBEDO = glow * 7.0 * pulse;
}
"""
	var firefly_material := ShaderMaterial.new()
	firefly_material.shader = shader
	var firefly_mesh := _sphere(0.08, 6, 3)
	firefly_mesh.material = firefly_material
	_spawn(fireflies, firefly_mesh, Vector3.ZERO, false)
	_counts.fireflies = fireflies.count

	var mist := FogVolume.new()
	mist.size = Vector3(210, 7, 210)
	mist.position = Vector3(LAKE.x, WATER + 2.5, LAKE.y)
	var mist_material := FogMaterial.new()
	mist_material.density = 0.05
	mist_material.albedo = Color(0.75, 0.85, 1.0)
	mist.material = mist_material
	add_child(mist)


# --- Environment ----------------------------------------------------------------

func _lighting() -> void:
	var world := WorldEnvironment.new()
	_environment = Environment.new()
	var sky := Sky.new()
	var sky_material := ProceduralSkyMaterial.new()
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 190
	_environment.background_mode = Environment.BG_SKY
	_environment.tonemap_mode = Environment.TONE_MAPPER_ACES
	_environment.ssao_enabled = true
	_environment.ssao_radius = 1.6
	_environment.ssao_intensity = 2.2
	_environment.glow_enabled = true
	if night:
		sky_material.sky_top_color = Color(0.008, 0.012, 0.035)
		sky_material.sky_horizon_color = Color(0.035, 0.05, 0.1)
		sky_material.ground_horizon_color = Color(0.03, 0.04, 0.07)
		sky_material.ground_bottom_color = Color(0.01, 0.01, 0.02)
		sky_material.sun_angle_max = 2.0
		sun.rotation_degrees = Vector3(-38, 140, 0)
		sun.light_color = Color(0.55, 0.65, 1.0)
		sun.light_energy = 0.32
		sun.light_volumetric_fog_energy = 0.7
		_environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		_environment.ambient_light_color = Color(0.07, 0.09, 0.16)
		_environment.ambient_light_energy = 1.0
		_environment.volumetric_fog_enabled = true
		_environment.volumetric_fog_density = 0.011
		_environment.volumetric_fog_albedo = Color(0.8, 0.85, 1.0)
		_environment.volumetric_fog_emission = Color(0.004, 0.006, 0.012)
		_environment.volumetric_fog_length = 220.0
		_environment.volumetric_fog_sky_affect = 0.35
		_environment.glow_intensity = 1.1
		_environment.glow_bloom = 0.08
		_environment.tonemap_exposure = 1.25
	else:
		sky_material.sky_top_color = Color(0.2, 0.4, 0.78)
		sky_material.sky_horizon_color = Color(0.7, 0.76, 0.86)
		sky_material.ground_horizon_color = Color(0.6, 0.66, 0.74)
		sun.rotation_degrees = Vector3(-30, -42, 0)
		sun.light_color = Color(1.0, 0.9, 0.74)
		sun.light_energy = 1.55
		_environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		_environment.ambient_light_energy = 0.5
		_environment.tonemap_exposure = 0.82
		_environment.adjustment_enabled = true
		_environment.adjustment_saturation = 1.15
		_environment.adjustment_contrast = 1.06
		_environment.fog_enabled = true
		_environment.fog_light_color = Color(0.68, 0.76, 0.88)
		_environment.fog_density = 0.0011
		_environment.fog_aerial_perspective = 0.65
		_environment.fog_sky_affect = 0.2
		_environment.fog_sun_scatter = 0.25
		_environment.ssr_enabled = true
		_environment.ssr_max_steps = 96
		_environment.glow_intensity = 0.45
	sky.sky_material = sky_material
	_environment.sky = sky
	world.environment = _environment
	add_child(world)
	add_child(sun)


# --- Camera route and HUD --------------------------------------------------------

func _build_route() -> void:
	# x, clearance above ground/water, z. Closed Catmull-Rom loop.
	_route = [
		Vector3(-10, 16, 215), Vector3(_road_x(140) + 4, 8, 140), Vector3(VILLAGE.x - 6, 13, VILLAGE.y + 26),
		Vector3(_road_x(-10) + 2, 7, -10), Vector3(70, 11, -55), Vector3(LAKE.x, 6, LAKE.y + 10),
		Vector3(205, 24, -140), Vector3(140, 62, -240), Vector3(-60, 55, -215), Vector3(-175, 17, -70),
		Vector3(-150, 11, 95), Vector3(-70, 15, 205)]


func _catmull(a: Vector3, b: Vector3, c: Vector3, d: Vector3, u: float) -> Vector3:
	return 0.5 * (2.0 * b + (c - a) * u + (2.0 * a - 5.0 * b + 4.0 * c - d) * u * u + (3.0 * b - a - 3.0 * c + d) * u * u * u)


func _route_point(t: float) -> Vector3:
	var n := _route.size()
	var s := fposmod(t / SEGMENT_TIME, float(n))
	var i := int(s)
	return _catmull(_route[(i + n - 1) % n], _route[i], _route[(i + 1) % n], _route[(i + 2) % n], s - i)


func _camera_pose(t: float) -> Transform3D:
	var p := _route_point(t)
	# Clear the highest ground slightly behind and ahead, so hills are anticipated.
	var lowest := -INF
	for dt in [-0.6, 0.0, 0.7, 1.4, 2.1]:
		var q := _route_point(t + dt)
		var ground := maxf(_ground(q.x, q.z), WATER)
		var canopy := 17.0 * _forest(q.x, q.z) if ground > WATER + 1.2 and ground < 52.0 else 0.0
		lowest = maxf(lowest, ground + maxf(p.y, canopy + 5.0))
	var position := Vector3(p.x, lowest, p.z)
	var ahead := _route_point(t + 2.2)
	var target := Vector3(ahead.x, maxf(_ground(ahead.x, ahead.z), WATER) + ahead.y * 0.55, ahead.z)
	return Transform3D(Basis.looking_at(target - position), position)


func _set_gpu_geometry(enabled: bool, clod := true, occlusion := true) -> void:
	_gpu_geometry = enabled
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", enabled)
	# Shadow-pass culling is a separate opt-in (no measured gain in this valley).
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/shadows", enabled and OS.get_cmdline_user_args().has("--shadow-culling"))
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", enabled and clod)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", enabled and clod and occlusion)


func _make_hud() -> void:
	var overlay := CanvasLayer.new()
	add_child(overlay)
	_hud_panel = PanelContainer.new()
	_hud_panel.position = Vector2(16, 14)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.02, 0.03, 0.05, 0.62)
	style.set_corner_radius_all(8)
	style.set_content_margin_all(12)
	style.border_color = Color(1, 1, 1, 0.12)
	style.set_border_width_all(1)
	_hud_panel.add_theme_stylebox_override("panel", style)
	overlay.add_child(_hud_panel)
	_hud = Label.new()
	_hud.add_theme_font_size_override("font_size", 15)
	_hud.add_theme_color_override("font_color", Color(0.93, 0.95, 1.0))
	_hud_panel.add_child(_hud)


func _stage_text(stage: String) -> String:
	return "on" if _stage_state.get(stage, false) else "off"


func _update_hud() -> void:
	var viewport := get_viewport()
	var rid := viewport.get_viewport_rid()
	var gpu := RenderingServer.viewport_get_measured_render_time_gpu(rid)
	var cpu := RenderingServer.viewport_get_measured_render_time_cpu(rid) + RenderingServer.get_frame_setup_time_cpu()
	var draws := viewport.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME)
	var shadow_draws := viewport.get_render_info(Viewport.RENDER_INFO_TYPE_SHADOW, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME)
	var primitives := viewport.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME)
	var title := "MOONLIT VALLEY  (volumetric fog, %d local lights, emissives)" % _local_lights.size() if night else "SUNLIT VALLEY  (sky, fog, SSAO, SSR water, glow)"
	var lines := PackedStringArray()
	lines.append("%s  |  %d x %d m" % [title, int(SIZE), int(SIZE)])
	lines.append("%d trees   %d bushes   %d grass tufts   %d flowers   %d CLOD rocks   CLOD terrain %dk tris" % [_counts.trees, _counts.bushes, _counts.grass, _counts.flowers, _counts.rocks, _terrain_triangles / 1000])
	if night:
		lines.append("%d glowing mushrooms   %d crystals   %d fireflies (custom vertex shader: conventional path)" % [_counts.mushrooms, _counts.crystals, _counts.fireflies])
	lines.append("FPS %d   CPU %.2f ms   GPU %.2f ms   draws %d (+%d shadow)   %.2f M primitives submitted" % [Engine.get_frames_per_second(), cpu, gpu, draws, shadow_draws, primitives / 1.0e6])
	lines.append("GPU geometry [G] %s   |   instance culling %s   cluster LOD %s   HZB occlusion %s   (cull %.3f ms)" % ["ENABLED" if _gpu_geometry else "DISABLED", _stage_text(STAGES[0]), _stage_text(STAGES[1]), _stage_text(STAGES[2]), _stage_state.get("cull_ms", 0.0)])
	var stats := RenderingServer.viewport_get_gpu_geometry_statistics(rid)
	if not stats.is_empty():
		lines.append("GPU visible: %d / %d instances culled (+%d in fully visible patches)   CLOD %d / %d groups, %.2f M triangles   HZB rejected %d groups" % [stats.multimesh_instances_visible, stats.multimesh_instances_tested, stats.multimesh_instances_fully_visible, stats.clod_groups_visible, stats.clod_groups_tested, stats.clod_triangles_visible / 1.0e6, stats.hzb_groups_occluded])
		var per_lod: PackedInt64Array = stats.get("multimesh_instances_visible_per_lod", PackedInt64Array())
		lines.append("Per-instance LOD levels %s   shadow casters %d / %d culled in %d surfaces" % [per_lod, stats.get("shadow_multimesh_instances_visible", 0), stats.get("shadow_multimesh_instances_tested", 0), stats.get("shadow_multimesh_surfaces_culled", 0)])
	var far_kind := "static splats"
	if _far_gpu:
		var splats := 0
		for patch in _far_patches:
			splats += patch.proxy.get_splat_count() if patch.proxy.visible else 0
		far_kind = "GPU levels, %d splats drawn" % splats
	lines.append("Voxel far field [P] %s: %d patch proxies (%s), %d voxels, beyond %d m (%s)   ground-cover distance thinning %s" % ["ON" if _far_field else "OFF", _counts.far_proxies, far_kind, _counts.far_voxels, int(FAR_DISTANCE), "hard cut" if _far_hard_cut else "%d m cross-fade" % int(2.0 * FAR_OVERLAP), "ON" if _thinning else "OFF"])
	lines.append("GPU grass [B] %s: %d blades within %d m, %.1f MiB" % ["ON" if _grass_field.visible else "OFF", _grass_field.get_generated_blade_count() if _grass_field.visible else 0, int(_grass_field.radius), _grass_field.get_gpu_memory_usage() / 1048576.0])
	lines.append("%s   F auto/free   RMB look   WASD QE fly   Shift fast   Wheel speed%s   H hide" % ["AUTO FLY-THROUGH" if _autopilot else "FREE FLIGHT", "   V volumetric fog   L lights" if night else ""])
	_hud.text = "\n".join(lines)


func _read_profile() -> void:
	# Render thread. Reads RD timestamps of the frame that just retired; never
	# blocks. Must run before any texture readback, which resets the results.
	var rd := RenderingServer.get_rendering_device()
	var stages := {}
	var cull_start := -1
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		stages[name] = true
		if name == STAGES[0]:
			cull_start = rd.get_captured_timestamp_gpu_time(i)
		elif name == "MultiMesh Frustum Culling End" and cull_start >= 0:
			stages["cull_ms"] = (rd.get_captured_timestamp_gpu_time(i) - cull_start) / 1.0e6
	profile_ready.emit.call_deferred(stages)


# --- Lifecycle --------------------------------------------------------------------

func _ready() -> void:
	_smoke = OS.get_cmdline_user_args().has("--forest-map-smoke")
	_bench = OS.get_cmdline_user_args().has("--forest-map-bench")
	# --far-compare: screenshots of fixed views for far-field image comparisons
	# (run once without --far-field for the source reference).
	_far_compare = OS.get_cmdline_user_args().has("--far-compare")
	_bench = _bench or _far_compare
	# Vegetation uses Godot index LODs like imported assets; --no-mesh-lods disables them.
	_mesh_lods = not OS.get_cmdline_user_args().has("--no-mesh-lods")
	# Voxel far field: on when flying and in the smoke; the bench opts in with --far-field.
	_far_field = OS.get_cmdline_user_args().has("--far-field") or not _bench
	_thinning = OS.get_cmdline_user_args().has("--thinning") or not _bench
	_gpu_grass = OS.get_cmdline_user_args().has("--gpu-grass") or not _bench
	# The far field cross-fades by default; --far-hard-cut restores the overlap switch.
	_far_hard_cut = OS.get_cmdline_user_args().has("--far-hard-cut")
	_far_gpu = OS.get_cmdline_user_args().has("--far-gpu") or not (_bench or OS.get_cmdline_user_args().has("--far-static"))
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--far-res="):
			_far_gpu_resolution = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--far-px="):
			_far_gpu_pixels = arg.get_slice("=", 1).to_float()
		elif arg.begins_with("--view-time="):
			_view_time = arg.get_slice("=", 1).to_float()
	# Samples at half a finest voxel apart.
	_far_builder.sample_spacing = PATCH / (_far_gpu_resolution if _far_gpu else FAR_RESOLUTION) * 0.5
	_smoke = _smoke or _bench # Both drive the camera themselves.
	get_window().size = Vector2i(1280, 720) if _smoke else Vector2i(1600, 900)
	get_window().move_to_center()
	# FXAA keeps the HZB path eligible; MSAA and TAA jitter fall back by design.
	get_viewport().screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	_set_gpu_geometry(true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", 1.0)
	_random.seed = 271828 if night else 161803
	for setup in [[_hills, 7, 0.0032, 4], [_ridges, 13, 0.006, 4], [_detail, 19, 0.04, 2], [_density, 23, 0.011, 3]]:
		var noise: FastNoiseLite = setup[0]
		noise.seed = setup[1]
		noise.frequency = setup[2]
		noise.fractal_octaves = setup[3]
	_ridges.fractal_type = FastNoiseLite.FRACTAL_RIDGED
	_village_height = maxf(_raw_height(VILLAGE.x, VILLAGE.y), WATER + 3.0)
	var started := Time.get_ticks_msec()
	_lighting()
	var terrain := MeshInstance3D.new()
	terrain.mesh = _terrain_mesh()
	if terrain.mesh.generate_cluster_lods() != OK:
		_failures.append("terrain cluster LOD generation failed")
	add_child(terrain)
	_add_grass_field()
	_water()
	_populate()
	_place_rocks()
	if night:
		_night_props()
	print("FOREST_MAP_BUILD: %s, %d ms, %s" % ["night" if night else "day", Time.get_ticks_msec() - started, _counts])
	_build_route()
	_camera = Camera3D.new()
	_camera.far = 900
	_camera.fov = 72
	add_child(_camera)
	_camera.current = true
	_camera.transform = _camera_pose(maxf(_view_time, 0.0))
	_make_hud()
	_update_hud()
	if _far_compare:
		_run_far_compare()
	elif _bench:
		_run_bench()
	elif _smoke:
		_run_smoke()


func _process(delta: float) -> void:
	var time := Time.get_ticks_msec() / 1000.0
	if night:
		_campfire.light_energy = 5.0 + 1.1 * sin(time * 13.0) + 0.7 * sin(time * 7.3 + 1.0)
		_searchlight.rotation = Vector3(deg_to_rad(-16), time * 0.3, 0)
	if _smoke:
		return
	if _view_time >= 0.0:
		pass
	elif _autopilot:
		_route_time += delta * _speed / 22.0
		_camera.transform = _camera.transform.interpolate_with(_camera_pose(_route_time), minf(1.0, delta * 4.0))
	else:
		var direction := Vector3(float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A)), float(Input.is_physical_key_pressed(KEY_E)) - float(Input.is_physical_key_pressed(KEY_Q)), float(Input.is_physical_key_pressed(KEY_S)) - float(Input.is_physical_key_pressed(KEY_W)))
		_camera.position += (_camera.basis * direction).normalized() * _speed * (3.0 if Input.is_physical_key_pressed(KEY_SHIFT) else 1.0) * delta
	_hud_timer -= delta
	if _hud_timer <= 0.0:
		_hud_timer = 0.25
		RenderingServer.call_on_render_thread(_read_profile)
		_stage_state = await profile_ready
		_update_hud()


func _unhandled_input(event: InputEvent) -> void:
	if _smoke:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		match event.physical_keycode:
			KEY_F:
				_autopilot = not _autopilot
				if not _autopilot:
					_camera.rotation.z = 0
			KEY_G:
				_set_gpu_geometry(not _gpu_geometry)
			KEY_H:
				_hud_panel.visible = not _hud_panel.visible
			KEY_V:
				_environment.volumetric_fog_enabled = night and not _environment.volumetric_fog_enabled
			KEY_L:
				for light in _local_lights:
					light.visible = not light.visible
			KEY_B:
				_grass_field.visible = not _grass_field.visible
			KEY_P:
				_far_field = not _far_field
				for patch in _far_patches:
					_apply_far_field(patch)
			KEY_ESCAPE:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if event.pressed else Input.MOUSE_MODE_VISIBLE
			if event.pressed:
				_autopilot = false
		if event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_speed = minf(_speed * 1.25, 250)
		if event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_speed = maxf(_speed / 1.25, 2)
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_camera.rotation.y -= event.relative.x * 0.0025
		_camera.rotation.x = clampf(_camera.rotation.x - event.relative.y * 0.0025, -1.5, 1.5)


# --- Automated smoke ----------------------------------------------------------------

func _settle(frames: int) -> Dictionary:
	for frame in frames:
		await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(_read_profile)
	return await profile_ready


func _capture() -> Image:
	return get_viewport().get_texture().get_image()


func _gpu_ms(frames: int) -> float:
	var total := 0.0
	for frame in frames:
		await RenderingServer.frame_post_draw
		total += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
	return total / frames


func _bright_pixels(image: Image, threshold: float) -> int:
	var count := 0
	for y in range(0, image.get_height(), 4):
		for x in range(0, image.get_width(), 4):
			count += int(image.get_pixel(x, y).get_luminance() > threshold)
	return count


func _run_smoke() -> void:
	const SAMPLES := 8
	var variant := "night" if night else "day"
	var driver := RenderingServer.get_current_rendering_driver_name()
	var output := ProjectSettings.globalize_path("res://../../devtools/out/forest_map")
	DirAccess.make_dir_recursive_absolute(output)
	var seen := {}
	var previous := PackedByteArray()
	var period := SEGMENT_TIME * _route.size()
	for sample in SAMPLES:
		_camera.transform = _camera_pose(period * sample / SAMPLES)
		# Profile first: get_image() stalls the GPU queue and clears timestamps.
		_stage_state = await _settle(24)
		_update_hud()
		await RenderingServer.frame_post_draw
		var image := _capture()
		var present := PackedStringArray()
		for stage in STAGES:
			if _stage_state.get(stage, false):
				seen[stage] = true
				present.append(stage)
		print("FOREST_MAP_VIEW: %s %d stages=%s" % [variant, sample, present])
		if image.get_data() == previous:
			_failures.append("view %d is identical to the previous view" % sample)
		previous = image.get_data()
		if image.save_png(output.path_join("%s_%s_%d.png" % [driver, variant, sample])) != OK:
			_failures.append("screenshot save failed")
	for stage in STAGES:
		if not seen.has(stage):
			_failures.append("no view dispatched '%s'" % stage)
	if _counts.trees < 20000 or _counts.grass < 20000 or _counts.rocks < 200:
		_failures.append("world population too small: %s" % _counts)

	# Informational single-run GPU time at one forest-road view; not a benchmark.
	_camera.transform = _camera_pose(SEGMENT_TIME * 3.0)
	await _settle(30)
	var enabled_ms := await _gpu_ms(60)
	_set_gpu_geometry(false)
	var disabled_stages := await _settle(30)
	var disabled_ms := await _gpu_ms(60)
	_set_gpu_geometry(true)
	for stage in STAGES:
		if disabled_stages.get(stage, false):
			_failures.append("disabled GPU geometry still dispatched '%s'" % stage)
	print("FOREST_MAP_TIMING: %s %s enabled_gpu_ms=%.2f disabled_gpu_ms=%.2f (one run, informational)" % [driver, variant, enabled_ms, disabled_ms])

	if night:
		_camera.transform = _camera_pose(SEGMENT_TIME * 2.0)
		await _settle(30)
		var lit := _capture()
		if _bright_pixels(lit, 0.6) < 150:
			_failures.append("night village view lacks bright emissive/light coverage")
		for light in _local_lights:
			light.visible = false
		await _settle(30)
		if _capture().get_data() == lit.get_data():
			_failures.append("local lights made no measurable contribution")
		for light in _local_lights:
			light.visible = true
		_environment.volumetric_fog_enabled = false
		await _settle(30)
		var no_fog := _capture()
		if no_fog.get_data() == lit.get_data():
			_failures.append("volumetric fog made no measurable contribution")
		_environment.volumetric_fog_enabled = true
		lit.save_png(output.path_join("%s_night_village.png" % driver))
		no_fog.save_png(output.path_join("%s_night_village_no_volumetric_fog.png" % driver))

	if _failures.is_empty():
		print("FOREST_MAP_TEST_PASS: %s %s, %s; %d views moved; culling/CLOD/HZB dispatched; disabled path clean" % [driver, variant, _counts, SAMPLES])
	else:
		for failure in _failures:
			printerr("FOREST_MAP_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)


# --- Benchmark ------------------------------------------------------------------------

func _percentile(values: Array[float], fraction: float) -> float:
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _run_far_compare() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var variant := ("gpu%d_%s" % [_far_gpu_resolution, String.num(_far_gpu_pixels)]) if _far_gpu else ("static" if _far_field else "source")
	var output := ProjectSettings.globalize_path("res://../../devtools/out/forest")
	DirAccess.make_dir_recursive_absolute(output)
	_hud_panel.visible = false
	for view in [0.0, 13.5, 27.0, 40.5]:
		_camera.transform = _camera_pose(view)
		await _settle(90)
		get_viewport().get_texture().get_image().save_png(output.path_join("far_%s_%s_%d.png" % [driver, variant, int(view * 10)]))
		var splats := 0
		var memory := 0
		for patch in _far_patches:
			if _far_gpu and patch.proxy.visible:
				splats += patch.proxy.get_splat_count()
				memory += patch.proxy.get_gpu_memory_usage()
		print("FOREST_FAR_COMPARE: %s %s view=%d splats=%d gpu_ms=%.2f far_gpu_mib=%.1f" % [driver, variant, int(view * 10), splats, RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()), memory / 1048576.0])
	get_tree().quit()


func _run_bench() -> void:
	# Fixed views and alternating mode order; per-frame viewport render-thread CPU
	# time (plus shared frame setup) and GPU time. Pipelines are warmed first.
	const FRAMES := 240
	const VIEWS: Array[float] = [0.0, 27.0, 40.5]
	const MODES: Array[String] = ["source", "culling", "culling+clod", "culling+clod+hzb"]
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	for view in VIEWS.size():
		_camera.transform = _camera_pose(VIEWS[view])
		for warm in MODES.size():
			_set_gpu_geometry(warm > 0, warm > 1, warm > 2)
			await _settle(40)
		var cpu := {}
		var gpu := {}
		var draws := {}
		for mode in MODES:
			cpu[mode] = [] as Array[float]
			gpu[mode] = [] as Array[float]
		for repeat in 2:
			for k in MODES.size():
				var m := k if repeat == 0 else MODES.size() - 1 - k
				_set_gpu_geometry(m > 0, m > 1, m > 2)
				await _settle(30)
				for frame in FRAMES:
					await RenderingServer.frame_post_draw
					cpu[MODES[m]].append(RenderingServer.viewport_get_measured_render_time_cpu(rid) + RenderingServer.get_frame_setup_time_cpu())
					gpu[MODES[m]].append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
				draws[MODES[m]] = get_viewport().get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME)
		print("FOREST_BENCH_PATCHES: %s view=%d %s grass_blades=%d" % [driver, view, _classify_patches(), _grass_field.get_generated_blade_count() if _grass_field.visible else 0])
		for mode in MODES:
			print("FOREST_BENCH: %s view=%d mode=%-17s cpu_p50=%.3f cpu_p95=%.3f gpu_p50=%.3f gpu_p95=%.3f draws=%d" % [driver, view, mode, _percentile(cpu[mode], 0.5), _percentile(cpu[mode], 0.95), _percentile(gpu[mode], 0.5), _percentile(gpu[mode], 0.95), draws[mode]])
		# Counters after timing: polling records for 120 frames, which the next
		# view's warm-up (160 frames) covers.
		_set_gpu_geometry(true)
		RenderingServer.viewport_get_gpu_geometry_statistics(rid)
		await _settle(10)
		print("FOREST_BENCH_STATS: %s view=%d %s" % [driver, view, RenderingServer.viewport_get_gpu_geometry_statistics(rid)])
	_set_gpu_geometry(true)
	get_tree().quit(0)


func _classify_patches() -> String:
	# Same test as the renderer: a MultiMesh fully inside every frustum plane
	# skips GPU culling; one crossing a plane is culled per instance on the GPU.
	var planes := _camera.get_frustum()
	var inside := 0
	var crossing := 0
	for child in get_children():
		var node := child as MultiMeshInstance3D
		if node == null:
			continue
		var box := node.global_transform * node.get_aabb()
		if node.visibility_range_end > 0.0 and _camera.global_position.distance_to(box.get_center()) > node.visibility_range_end + node.visibility_range_end_margin:
			continue
		var outside := false
		var all_in := true
		for plane in planes:
			if plane.distance_to(box.get_support(-plane.normal)) > 0.0:
				outside = true
				break
			if plane.distance_to(box.get_support(plane.normal)) > 0.0:
				all_in = false
		if outside:
			continue
		if all_in:
			inside += 1
		else:
			crossing += 1
	return "multimeshes_in_view=%d fully_inside=%d crossing=%d" % [inside + crossing, inside, crossing]
