extends Node
## Two-pass MultiMesh occlusion in the depth pre-pass
## (rendering/geometry/multimesh_gpu_culling/occlusion_two_pass) on the valley
## map, three fixed views: the depth passes must draw fewer instances than the
## frustum keeps, and the viewport GPU time is reported with and without it.
## Image equality is tested in multimesh_occlusion.tscn: the valley does not
## repeat exactly along a moving path even without two-pass (other systems
## keep history). Prints MULTIMESH_TWO_PASS lines and
## MULTIMESH_TWO_PASS_TEST_PASS or _FAIL.

const SETTING := "rendering/geometry/multimesh_gpu_culling/occlusion_two_pass"
const VIEWS: Array[float] = [0.0, 27.0, 40.5]

var _map: Node
var _camera: Camera3D
var _failures: Array[String] = []


func _check(label: String, ok: bool) -> void:
	print("MULTIMESH_TWO_PASS: %s %s" % ["PASS" if ok else "FAIL", label])
	if not ok:
		_failures.append(label)


func _stats() -> Dictionary:
	return RenderingServer.viewport_get_gpu_geometry_statistics(get_viewport().get_viewport_rid())


func _measure(view: float, two_pass: bool) -> Array:
	ProjectSettings.set_setting(SETTING, two_pass)
	_camera.transform = _map.call("_camera_pose", view)
	var rid := get_viewport().get_viewport_rid()
	var stats := {}
	for i in 60:
		await get_tree().process_frame
		stats = _stats()
	var total := 0.0
	for i in 200:
		await get_tree().process_frame
		total += RenderingServer.viewport_get_measured_render_time_gpu(rid)
	return [total / 200.0, _stats()]


func _ready() -> void:
	_map = load("res://forest_map.tscn").instantiate()
	add_child(_map)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	get_viewport().vrs_mode = Viewport.VRS_DISABLED
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	for i in 600:
		await get_tree().process_frame
	Engine.time_scale = 0.0
	_camera = get_viewport().get_camera_3d()

	for view in VIEWS:
		var off: Array = await _measure(view, false)
		var on: Array = await _measure(view, true)
		var off_again: Array = await _measure(view, false)
		var visible := int(on[1].get("multimesh_instances_visible", 0))
		var depth := int(on[1].get("multimesh_depth_instances", 0))
		var occluded := int(on[1].get("hzb_instances_occluded", 0))
		print("MULTIMESH_TWO_PASS: view %.1f: GPU %.3f ms one-pass (%.3f again), %.3f ms two-pass (%.0f%%); depth draws %d of %d frustum-visible instances, %d occluded" % [view, off[0], off_again[0], on[0], 100.0 * on[0] / off[0], depth, visible, occluded])
		_check("view %.1f: depth passes draw no more than the frustum keeps" % view, depth > 0 and depth <= visible)
		_check("view %.1f: depth passes skip occluded instances" % view, occluded == 0 or depth < visible)
	ProjectSettings.set_setting(SETTING, true)
	print("MULTIMESH_TWO_PASS_TEST_FAIL" if not _failures.is_empty() else "MULTIMESH_TWO_PASS_TEST_PASS")
	get_tree().quit(_failures.size())
