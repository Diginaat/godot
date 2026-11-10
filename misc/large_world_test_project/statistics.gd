extends Node3D
## Native GPU geometry statistics: exact MultiMesh/CLOD/HZB counts from
## RenderingServer.viewport_get_gpu_geometry_statistics, read asynchronously.

var _failures: Array[String] = []
var _groups := 0
var _source_triangles := 0
var _spot: SpotLight3D


func _material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	return material


func _multimesh(count: int, mesh: Mesh) -> MultiMesh:
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = mesh
	multimesh.instance_count = count
	return multimesh


func _ready() -> void:
	# Exact counts of the culling paths alone: resident cluster data (no
	# streaming pool) and no MultiMesh or GPU path occlusion, which are on by
	# default.
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/streaming/enabled", false)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/occlusion", false)
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/occlusion", false)
	var box := BoxMesh.new()
	box.size = Vector3.ONE * 0.3
	box.material = _material(Color(0.8, 0.3, 0.2))
	# 700 instances in front of the camera, 300 behind it: the MultiMesh crosses
	# the frustum, so it is GPU-culled with exactly 700 visible instances.
	var crossing := _multimesh(1000, box)
	for i in 1000:
		var position := Vector3((i % 35 - 17) * 0.4, (i / 35 - 10) * 0.4, -30) if i < 700 else Vector3((i % 10) * 0.5, 0, 30)
		crossing.set_instance_transform(i, Transform3D(Basis(), position))
	var crossing_node := MultiMeshInstance3D.new()
	crossing_node.multimesh = crossing
	add_child(crossing_node)
	# 50 instances fully inside the frustum keep their direct draw.
	var inside := _multimesh(50, box)
	for i in 50:
		inside.set_instance_transform(i, Transform3D(Basis(), Vector3(-3 + (i % 10) * 0.6, -4 + (i / 10) * 0.6, -20)))
	var inside_node := MultiMeshInstance3D.new()
	inside_node.multimesh = inside
	add_child(inside_node)
	var sphere := SphereMesh.new()
	sphere.radial_segments = 64
	sphere.rings = 32
	var arrays := sphere.get_mesh_arrays()
	var clod_mesh := ArrayMesh.new()
	clod_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	if clod_mesh.generate_cluster_lods() != OK:
		_failures.append("cluster LOD generation failed")
	var surface: Dictionary = clod_mesh.cluster_data.get_surface_data(0)
	_groups = surface.groups.size() / 5
	_source_triangles = surface.source_index_count / 3
	# A visible sphere, and one hidden behind an opaque wall for HZB rejection.
	for i in 2:
		var node := MeshInstance3D.new()
		node.mesh = clod_mesh
		node.position = Vector3(6, 0, -15) if i == 0 else Vector3(-8, 0, -40)
		node.material_override = _material(Color(0.2, 0.5, 0.9) if i == 0 else Color(0.3, 0.9, 0.3))
		add_child(node)
	var wall := MeshInstance3D.new()
	var wall_mesh := BoxMesh.new()
	wall_mesh.size = Vector3(14, 14, 0.5)
	wall.mesh = wall_mesh
	wall.position = Vector3(-8, 0, -25)
	wall.material_override = _material(Color(0.7, 0.7, 0.7))
	add_child(wall)
	# Shadow-pass culling: a spot light looks straight down at (0, 20, -70) with
	# a 20 m range. 200 casters lie 12 m below it, 100 lie 30 m below it (beyond
	# the shadow far plane). The camera sees all 300, so the main view keeps a
	# direct draw while the spot shadow pass culls exactly 100.
	var casters := _multimesh(300, box)
	for i in 300:
		var position := Vector3((i % 20 - 10) * 0.2, 8, -70 + (i / 20 - 5) * 0.4) if i < 200 else Vector3((i % 10 - 5) * 0.4, -10, -70 + (i / 10 - 15) * 0.4)
		casters.set_instance_transform(i, Transform3D(Basis(), position))
	var casters_node := MultiMeshInstance3D.new()
	casters_node.multimesh = casters
	add_child(casters_node)
	var spot := SpotLight3D.new()
	spot.position = Vector3(0, 20, -70)
	spot.rotation_degrees = Vector3(-90, 0, 0)
	spot.spot_range = 20
	spot.spot_angle = 25
	spot.shadow_enabled = true
	add_child(spot)
	_spot = spot
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-40, 30, 0)
	add_child(light)
	var camera := Camera3D.new()
	camera.far = 100
	add_child(camera)
	camera.current = true
	_run.call_deferred()


func _process(_delta: float) -> void:
	# Positional shadows are cached in the atlas and re-rendered only on change;
	# a 0.1 mm nudge per frame keeps the spot shadow pass (and its culling) live.
	_spot.position.x = 0.0001 * float(Engine.get_frames_drawn() % 2)


func _settings(enabled: bool, error_pixels: float, occlusion: bool) -> void:
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", enabled)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/shadows", enabled)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", enabled)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/error_pixels", error_pixels)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", occlusion)


func _statistics(after_frame: int) -> Dictionary:
	# Polling enables the counters; results arrive a few frames later.
	var viewport := get_viewport().get_viewport_rid()
	for attempt in 60:
		await RenderingServer.frame_post_draw
		var result := RenderingServer.viewport_get_gpu_geometry_statistics(viewport)
		if not result.is_empty() and int(result.frame) > after_frame:
			return result
	_failures.append("statistics never arrived")
	return {}


func _expect(stats: Dictionary, key: String, expected: int, label: String) -> void:
	if int(stats.get(key, -1)) != expected:
		_failures.append("%s: %s = %s, expected %d" % [label, key, stats.get(key), expected])


func _expect_monitor(monitor: Performance.Monitor, expected: int, label: String) -> void:
	var value := int(Performance.get_monitor(monitor))
	if value != expected:
		_failures.append("%s: monitor %d = %d, expected %d" % [label, monitor, value, expected])


func _run() -> void:
	var start := Engine.get_frames_drawn()
	_settings(true, 0.0, false)
	var fine := await _statistics(start + 5)
	_expect(fine, "multimesh_surfaces_culled", 1, "fine")
	_expect(fine, "multimesh_instances_tested", 1000, "fine")
	_expect(fine, "multimesh_instances_visible", 700, "fine")
	_expect(fine, "multimesh_surfaces_fully_visible", 2, "fine")
	_expect(fine, "multimesh_instances_fully_visible", 350, "fine")
	_expect(fine, "shadow_multimesh_surfaces_culled", 1, "fine shadow")
	_expect(fine, "shadow_multimesh_instances_tested", 300, "fine shadow")
	_expect(fine, "shadow_multimesh_instances_visible", 200, "fine shadow")
	_expect(fine, "clod_surfaces", 2, "fine")
	_expect(fine, "clod_groups_tested", 2 * _groups, "fine")
	# Leaves preserve source triangles: the 0 px cut is the complete source mesh.
	_expect(fine, "clod_triangles_visible", 2 * _source_triangles, "fine")
	_expect(fine, "hzb_groups_occluded", 0, "fine without occlusion")
	# Performance monitors read the same root-viewport counters.
	_expect_monitor(Performance.RENDER_GPU_CULLED_INSTANCES_VISIBLE, 700 + 350, "fine")
	_expect_monitor(Performance.RENDER_CLOD_TRIANGLES_VISIBLE, 2 * _source_triangles, "fine")

	_settings(true, 64.0, false)
	var coarse := await _statistics(Engine.get_frames_drawn() + 3)
	var coarse_triangles := int(coarse.get("clod_triangles_visible", 0))
	if coarse_triangles <= 0 or coarse_triangles >= 2 * _source_triangles:
		_failures.append("coarse cut triangles %d not between 0 and %d" % [coarse_triangles, 2 * _source_triangles])
	if int(coarse.get("clod_groups_selected", 0)) >= int(fine.get("clod_groups_selected", 0)):
		_failures.append("coarse cut selected no fewer groups than the fine cut")

	_settings(true, 0.0, true)
	var occluded := await _statistics(Engine.get_frames_drawn() + 3)
	var hidden_groups := int(occluded.get("hzb_groups_occluded", 0))
	var hidden_triangles := int(occluded.get("hzb_triangles_occluded", 0))
	if hidden_groups <= 0 or hidden_triangles <= 0 or hidden_triangles > _source_triangles:
		_failures.append("HZB occluded %d groups / %d triangles; expected some of the hidden sphere only" % [hidden_groups, hidden_triangles])
	_expect_monitor(Performance.RENDER_HZB_TRIANGLES_OCCLUDED, hidden_triangles, "occluded")

	_settings(false, 1.0, false)
	var disabled := await _statistics(Engine.get_frames_drawn() + 3)
	for key in ["multimesh_instances_tested", "shadow_multimesh_instances_tested", "clod_groups_tested", "hzb_groups_occluded"]:
		_expect(disabled, key, 0, "disabled")

	if _failures.is_empty():
		print("STATISTICS_TEST_PASS: 1000 tested / 700 visible instances, 350 fully visible, spot shadow 300 tested / 200 visible, %d groups x2, %d source triangles x2 at 0 px, coarse %d, HZB %d groups / %d triangles, monitors match, disabled zero" % [_groups, _source_triangles, coarse_triangles, hidden_groups, hidden_triangles])
	else:
		for failure in _failures:
			printerr("STATISTICS_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
