extends Node3D
## HZB occlusion benchmark (devtools/test/hzb_bench.ps1). A street grid where
## buildings hide dense CLOD objects and MultiMesh street props (GPU-culled).
## Four modes: occlusion off, CLOD single pass (every selected group in depth,
## colour trimmed), CLOD two passes (groups occluded when last tested are left
## out of depth too) and two passes plus MultiMesh instance occlusion (colour
## only). Three fixed views,
## alternating mode order, pipelines warmed first. Prints GPU frame time and
## the depth-stage times from RD timestamps. Not a correctness test.

signal stages_ready(stages: Dictionary)

const BLOCKS := 8
const SPACING := 26.0
const BUILDING := 18.0
const FRAMES := 240
const MODES: Array[String] = ["off", "single", "two_pass", "two_pass+mm"]
const VIEWS: Array[String] = ["street", "corner", "aerial"]
var _camera: Camera3D
var _objects := 0
var _source_triangles := 0
var _props := 0
var _prop_triangles := 0


func _dense_mesh() -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radius = 2.5
	sphere.height = 5.0
	sphere.radial_segments = 256
	sphere.rings = 128
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	if mesh.generate_cluster_lods() != OK:
		printerr("HZB_BENCH_FAIL: cluster LOD generation failed")
	return mesh


func _ready() -> void:
	get_window().size = Vector2i(1920, 1080)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/enabled", true)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.45, 0.55, 0.7)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.5)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	# No shadows: shadow passes draw CLOD meshes from source geometry, which
	# would dominate the frame and does not depend on occlusion.
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * (BLOCKS + 2) * SPACING
	ground.mesh = plane
	add_child(ground)
	var building_material := StandardMaterial3D.new()
	building_material.albedo_color = Color(0.62, 0.6, 0.57)
	var half := (BLOCKS - 1) * 0.5
	for i in BLOCKS:
		for j in BLOCKS:
			var height := 20.0 + float((i * 7 + j * 13) % 5) * 5.0
			var box := BoxMesh.new()
			box.size = Vector3(BUILDING, height, BUILDING)
			box.material = building_material
			var building := MeshInstance3D.new()
			building.mesh = box
			building.position = Vector3((i - half) * SPACING, height * 0.5, (j - half) * SPACING)
			add_child(building)
	# Dense objects at every street crossing and halfway along every street.
	var dense := _dense_mesh()
	for i in 2 * BLOCKS + 1:
		for j in 2 * BLOCKS + 1:
			if i % 2 == 1 and j % 2 == 1:
				continue # Inside a block.
			var node := MeshInstance3D.new()
			node.mesh = dense
			# One material per object: instances that share mesh and material are
			# batched into one render element, which CLOD selection skips.
			var dense_material := StandardMaterial3D.new()
			dense_material.albedo_color = Color.from_hsv(fmod(float(_objects) * 0.071, 1.0), 0.6, 0.85)
			node.material_override = dense_material
			node.position = Vector3((i * 0.5 - BLOCKS * 0.5) * SPACING, 2.5, (j * 0.5 - BLOCKS * 0.5) * SPACING)
			add_child(node)
			_objects += 1
	_source_triangles = _objects * dense.get_faces().size() / 3
	# Street props: one MultiMesh per street, on both sidewalks every 2 m.
	var prop := SphereMesh.new()
	prop.radius = 0.5
	prop.height = 1.0
	prop.radial_segments = 32
	prop.rings = 16
	var prop_mesh := ArrayMesh.new()
	prop_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, prop.get_mesh_arrays())
	var prop_material := StandardMaterial3D.new()
	prop_material.albedo_color = Color(0.2, 0.55, 0.25)
	prop_mesh.surface_set_material(0, prop_material)
	var length := BLOCKS * SPACING
	var sidewalk := (SPACING - BUILDING) * 0.5 - 0.8
	for line in BLOCKS + 1:
		for axis in 2:
			var transforms: Array[Transform3D] = []
			var offset := (line - BLOCKS * 0.5) * SPACING
			for step in int(length / 2.0):
				var along := -length * 0.5 + 1.0 + 2.0 * float(step)
				for side in [-sidewalk, sidewalk]:
					var position := Vector3(offset + side, 0.5, along) if axis == 0 else Vector3(along, 0.5, offset + side)
					transforms.append(Transform3D(Basis(), position))
			var multimesh := MultiMesh.new()
			multimesh.transform_format = MultiMesh.TRANSFORM_3D
			multimesh.mesh = prop_mesh
			multimesh.instance_count = transforms.size()
			for i in transforms.size():
				multimesh.set_instance_transform(i, transforms[i])
			var node := MultiMeshInstance3D.new()
			node.multimesh = multimesh
			add_child(node)
			_props += transforms.size()
	_prop_triangles = _props * prop_mesh.get_faces().size() / 3
	_camera = Camera3D.new()
	_camera.far = 600.0
	add_child(_camera)
	_camera.current = true
	_run.call_deferred()


func _view(index: int) -> void:
	var edge := BLOCKS * 0.5 * SPACING
	if index == 0:
		# Down the central avenue: its own objects are visible, the parallel
		# streets are hidden by the buildings.
		_camera.position = Vector3(0, 1.8, edge + 8.0)
		_camera.look_at(Vector3(0, 1.8, -edge))
	elif index == 1:
		# At a crossing, looking diagonally into the blocks.
		_camera.position = Vector3(-SPACING * 2.0, 1.8, SPACING)
		_camera.look_at(Vector3(edge, 1.8, -edge))
	else:
		# From above: almost everything is visible (occlusion overhead).
		_camera.position = Vector3(0, 160, 140)
		_camera.look_at(Vector3.ZERO)


func _mode(index: int) -> void:
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_enabled", index > 0)
	ProjectSettings.set_setting("rendering/geometry/cluster_lod/occlusion_two_pass", index >= 2)
	ProjectSettings.set_setting("rendering/geometry/multimesh_gpu_culling/occlusion", index == 3)


func _read_stages() -> void:
	# Render thread. Timestamps of the frame that just retired; never blocks.
	var rd := RenderingServer.get_rendering_device()
	var times := {}
	for i in rd.get_captured_timestamps_count():
		var name := rd.get_captured_timestamp_name(i)
		if not times.has(name):
			times[name] = rd.get_captured_timestamp_gpu_time(i)
	var stages := {}
	var start: int = times.get("GPU Geometry Depth Pre-Pass", -1)
	var end: int = times.get("GPU Geometry Depth Pre-Pass End", -1)
	if start >= 0 and end >= start:
		var occlusion: int = times.get("MultiMesh HZB Occlusion", -1)
		var occlusion_end: int = times.get("MultiMesh HZB Occlusion End", -1)
		stages.mm_occlusion = (occlusion_end - occlusion) / 1.0e6 if occlusion >= 0 and occlusion_end >= occlusion else 0.0
		# Order inside the pre-pass: split, phase 1, HZB build and CLOD test,
		# phase 2, MultiMesh test. Each stage ends where the next present one starts.
		var marks: Array[int] = []
		for name in ["Mesh CLOD HZB", "Mesh CLOD Phase 2 Depth", "MultiMesh HZB Occlusion"]:
			marks.append(times.get(name, -1))
		var next := func(from: int) -> int:
			for k in range(from, marks.size()):
				if marks[k] >= 0:
					return marks[k]
			return end
		stages.depth = (end - start) / 1.0e6 - stages.mm_occlusion
		stages.phase1 = (next.call(0) - start) / 1.0e6
		stages.hzb = (next.call(1) - marks[0]) / 1.0e6 if marks[0] >= 0 else 0.0
		stages.phase2 = (next.call(2) - marks[1]) / 1.0e6 if marks[1] >= 0 else 0.0
	var opaque: int = times.get("GPU Geometry Opaque Pass", -1)
	var opaque_end: int = times.get("GPU Geometry Opaque Pass End", -1)
	if opaque >= 0 and opaque_end >= opaque:
		stages.opaque = (opaque_end - opaque) / 1.0e6
	stages_ready.emit.call_deferred(stages)


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(fraction * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _run() -> void:
	var driver := RenderingServer.get_current_rendering_driver_name()
	var rid := get_viewport().get_viewport_rid()
	print("HZB_BENCH_SCENE: %s %dx%d, %d buildings, %d CLOD objects (%.1f M source triangles), %d MultiMesh props (%.1f M triangles), %d frames per mode and repeat" % [driver, get_window().size.x, get_window().size.y, BLOCKS * BLOCKS, _objects, _source_triangles / 1.0e6, _props, _prop_triangles / 1.0e6, FRAMES])
	const KEYS: Array[String] = ["gpu", "depth", "phase1", "hzb", "phase2", "mm_occlusion", "opaque"]
	for view in VIEWS.size():
		_view(view)
		for warm in MODES.size():
			_mode(warm)
			for frame in 60:
				await RenderingServer.frame_post_draw
		var samples := {}
		for mode in MODES:
			samples[mode] = {}
			for key in KEYS:
				samples[mode][key] = [] as Array[float]
		for repeat in 2:
			for k in MODES.size():
				var m := k if repeat == 0 else MODES.size() - 1 - k
				_mode(m)
				for frame in 30:
					await RenderingServer.frame_post_draw
				for frame in FRAMES:
					await RenderingServer.frame_post_draw
					samples[MODES[m]].gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
					RenderingServer.call_on_render_thread(_read_stages)
					var stages: Dictionary = await stages_ready
					for key in stages:
						samples[MODES[m]][key].append(stages[key])
		for mode in MODES:
			var row := PackedStringArray()
			for key in KEYS:
				row.append("%s_p50=%.3f" % [key, _percentile(samples[mode][key], 0.5)])
			row.append("gpu_p95=%.3f" % _percentile(samples[mode].gpu, 0.95))
			# A bench whose stages did not run measures nothing.
			if _percentile(samples[mode].gpu, 0.5) <= 0.0 or _percentile(samples[mode].depth, 0.5) <= 0.0 or _percentile(samples[mode].opaque, 0.5) <= 0.0:
				printerr("HZB_BENCH_FAIL: %s view=%s mode=%s has no GPU or stage times" % [driver, VIEWS[view], mode])
			print("HZB_BENCH: %s view=%s mode=%-11s %s" % [driver, VIEWS[view], mode, " ".join(row)])
		# Counters after timing: polling records for 120 frames, which the next
		# view's warm-up (240 frames) covers.
		for m in MODES.size():
			_mode(m)
			for frame in 8:
				await RenderingServer.frame_post_draw
			var after := Engine.get_frames_drawn()
			var stats := {}
			for attempt in 90:
				await RenderingServer.frame_post_draw
				stats = RenderingServer.viewport_get_gpu_geometry_statistics(rid)
				if not stats.is_empty() and int(stats.frame) > after:
					break
			var expect_occlusion := m > 0 and VIEWS[view] != "aerial"
			if int(stats.get("clod_groups_visible", 0)) <= 0 or (expect_occlusion and int(stats.get("hzb_groups_occluded", 0)) <= 0) or (m == 3 and int(stats.get("hzb_instances_occluded", 0)) <= 0):
				printerr("HZB_BENCH_FAIL: %s view=%s mode=%s did not exercise CLOD or occlusion: %s" % [driver, VIEWS[view], MODES[m], stats])
			print("HZB_BENCH_STATS: %s view=%s mode=%-11s clod_groups_visible=%d clod_triangles_visible=%d hzb_groups_occluded=%d hzb_triangles_occluded=%d hzb_triangles_deferred=%d hzb_triangles_recovered=%d multimesh_instances_visible=%d hzb_instances_occluded=%d" % [driver, VIEWS[view], MODES[m], stats.get("clod_groups_visible", -1), stats.get("clod_triangles_visible", -1), stats.get("hzb_groups_occluded", -1), stats.get("hzb_triangles_occluded", -1), stats.get("hzb_triangles_deferred", -1), stats.get("hzb_triangles_recovered", -1), stats.get("multimesh_instances_visible", -1), stats.get("hzb_instances_occluded", -1)])
	get_tree().quit(0)
