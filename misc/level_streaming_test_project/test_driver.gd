extends Node3D

# Acceptance test for LevelStreamer3D (docs/level-streaming.md). Drives the
# player down the street, into the shop (opens its door, switches its light
# off), straight back out, far enough away that the shop unloads, and back
# in; then crosses a preload boundary over and over, teleports back and forth
# without waiting, does a cold teleport with prefetch_position() and a
# teleport whose destination fails to load. Every step is checked; frame
# times, readiness delays and memory are written to a JSON file.
#
# User arguments (after --):
#   --results=<path>      where to write the JSON (default user://).
#   --preload-all         every cell always loaded: the comparison run.
#   --no-thread           instantiate cell scenes on the main thread.

const DRIVE := 15.0 # m/s
const WALK := 5.0
const FAST := 30.0
const SHOP_DOOR_Z := 60.0

@onready var streamer: LevelStreamer3D = $Streamer
@onready var player: Node3D = $Player

var preload_all := false
var results_path := "user://level_streaming_results.json"

var failures := PackedStringArray()
var phases := PackedStringArray(["startup"])
var phase := 0
var checking := true # Check readiness and collision on each physics frame.

var frame_usec := PackedInt64Array()
var frame_phase := PackedInt32Array()
var frame_boundary := PackedByteArray()
var last_frame_usec := 0
var boundary_until := -1
var peak_video_mem := 0.0
var not_ready_frames := 0
var no_ground_frames := 0
var events := {}
var activations := [] # [cell ID, phase, microseconds in add_child()]
var activation_begin := 0
var loads_in_phase := {} # Cell ID -> activations in the current phase.
var unloads_in_phase := {}


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg == "--preload-all":
			preload_all = true
		elif arg == "--no-thread":
			streamer.threaded_instantiation = false
		elif arg.begins_with("--results="):
			results_path = arg.trim_prefix("--results=")
	if preload_all:
		for id in streamer.get_cell_ids():
			streamer.get_cell(id).always_loaded = true
	streamer.cell_activating.connect(func(_id, _content): activation_begin = Time.get_ticks_usec())
	streamer.cell_activated.connect(_on_activated)
	streamer.cell_unloaded.connect(_on_unloaded)
	streamer.cell_failed.connect(func(id, _path): events["failed_" + id] = true)
	streamer.destination_ready.connect(func(_point): events["destination_ready"] = true)
	_run.call_deferred()


func _on_activated(id: StringName, _content: Node) -> void:
	activations.push_back([id, phases[phase], Time.get_ticks_usec() - activation_begin])
	loads_in_phase[id] = loads_in_phase.get(id, 0) + 1
	boundary_until = Engine.get_process_frames() + 2


func _on_unloaded(id: StringName) -> void:
	unloads_in_phase[id] = unloads_in_phase.get(id, 0) + 1
	boundary_until = Engine.get_process_frames() + 2


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	if last_frame_usec > 0:
		frame_usec.push_back(now - last_frame_usec)
		frame_phase.push_back(phase)
		frame_boundary.push_back(1 if Engine.get_process_frames() <= boundary_until and phases[phase] != "capture" else 0)
	last_frame_usec = now
	peak_video_mem = maxf(peak_video_mem, Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED))


func _fail(message: String) -> void:
	failures.push_back("%s: %s" % [phases[phase], message])
	printerr("FAIL ", failures[-1])


func _check(condition: bool, message: String) -> void:
	if not condition:
		_fail(message)


func _phase(name: String) -> void:
	var index := phases.find(name)
	if index < 0:
		phases.push_back(name)
		index = phases.size() - 1
	phase = index
	loads_in_phase.clear()
	unloads_in_phase.clear()
	if name != "capture":
		print("phase ", name)


func _frames(count: int) -> void:
	for i in count:
		await get_tree().process_frame


# Waits until p_condition is true or p_seconds pass; returns the wait in
# microseconds, or -1 on timeout.
func _until(condition: Callable, seconds: float) -> int:
	var begin := Time.get_ticks_usec()
	while not condition.call():
		if Time.get_ticks_usec() - begin > int(seconds * 1000000.0):
			return -1
		await get_tree().process_frame
	return Time.get_ticks_usec() - begin


func _ground(point: Vector3) -> Dictionary:
	var query := PhysicsRayQueryParameters3D.create(point + Vector3.UP * 2.0, point + Vector3.DOWN * 3.0)
	return get_world_3d().direct_space_state.intersect_ray(query)


func _check_position() -> void:
	if not checking:
		return
	var point := player.global_position
	if not streamer.is_position_ready(point):
		not_ready_frames += 1
		_fail("not ready at %s (cells %s)" % [point, streamer.get_cells_at(point)])
	if _ground(point).is_empty():
		no_ground_frames += 1
		_fail("no ground under %s" % point)


func _travel(to: Vector3, speed: float) -> void:
	var flat := Vector3(to.x, player.global_position.y, to.z) - player.global_position
	if flat.length() > 0.01:
		player.look_at(player.global_position + flat)
	while true:
		await get_tree().physics_frame
		var offset := to - player.global_position
		var step := speed / Engine.physics_ticks_per_second
		if offset.length() <= step:
			player.global_position = to
			_check_position()
			return
		player.global_position += offset.normalized() * step
		_check_position()


# Stops, looks along p_direction and returns a small copy of the frame.
# Its frames (the read back stalls) are kept out of the other phases.
func _capture(direction: Vector3, label: String) -> Image:
	var resume := phases[phase]
	_phase("capture")
	player.look_at(player.global_position + direction)
	await _frames(20)
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	image.save_png(results_path.get_base_dir().path_join("capture_%s.png" % label))
	image.resize(192, 108, Image.INTERPOLATE_BILINEAR)
	await _frames(2)
	_phase(resume)
	return image


func _image_difference(a: Image, b: Image) -> float:
	var total := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			total += absf(ca.r - cb.r) + absf(ca.g - cb.g) + absf(ca.b - cb.b)
	return total / (a.get_width() * a.get_height() * 3)


func _check_shop(shop: Node, label: String) -> void:
	if shop == null:
		_fail("%s: shop content missing" % label)
		return
	var sign := shop.get_node("Sign") as MeshInstance3D
	var material := sign.get_active_material(0) as StandardMaterial3D
	_check(material != null and material.albedo_texture != null and material.albedo_texture.get_width() == 512, "%s: shop sign texture" % label)
	var light := shop.get_node("Light") as OmniLight3D
	_check(light.is_inside_tree() and light.light_energy > 0.0, "%s: shop light" % label)
	var floor_hit := _ground(Vector3(22, 1, SHOP_DOOR_Z))
	_check(not floor_hit.is_empty() and shop.is_ancestor_of(floor_hit.collider), "%s: shop floor collision" % label)
	# Through the open doorway, the first thing hit is the far wall.
	var query := PhysicsRayQueryParameters3D.create(Vector3(5, 1.5, SHOP_DOOR_Z), Vector3(30, 1.5, SHOP_DOOR_Z))
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	_check(not hit.is_empty() and hit.collider.name == "WallEast", "%s: doorway ray hit %s" % [label, hit.get("collider")])
	var map := get_world_3d().navigation_map
	var target := Vector3(22, 0, SHOP_DOOR_Z + 4)
	var path := NavigationServer3D.map_get_path(map, Vector3(0, 0, 40), target, true)
	_check(path.size() > 0 and path[-1].distance_to(target) < 0.5, "%s: navigation path from the street into the shop (%s)" % [label, path])


func _check_consistency() -> void:
	for id in streamer.get_cell_ids():
		var cell := streamer.get_cell(id)
		var state := streamer.get_cell_state(id)
		var content := streamer.get_cell_content(id)
		var loaded := state == LevelStreamer3D.CELL_ACTIVE or state == LevelStreamer3D.CELL_READY
		_check(cell.get_child_count() == (1 if loaded else 0), "cell %s has %d children in state %d" % [id, cell.get_child_count(), state])
		_check(loaded == (content != null), "cell %s content %s in state %d" % [id, content, state])


func _run() -> void:
	var begin := Time.get_ticks_usec()
	var results := {}

	_phase("startup")
	checking = false
	var startup := await _until(func(): return streamer.is_position_ready(player.global_position), 20.0)
	_check(startup >= 0, "start cell never became ready")
	results["startup_ready_usec"] = startup
	await _frames(60)
	checking = true

	_phase("street")
	await _travel(Vector3(0, 1, SHOP_DOOR_Z), DRIVE)
	_check(streamer.is_cell_ready("shop") or preload_all, "shop not ready when the player reached its door")

	_phase("enter_shop")
	var shop := streamer.get_cell_content("shop")
	_check(shop != null, "shop content missing at its door")
	shop.get_node("Door").open = true
	await _travel(Vector3(16, 1, SHOP_DOOR_Z), WALK)
	shop.get_node("Light").on = false
	_check_shop(shop, "first visit")

	_phase("turn_back")
	await _travel(Vector3(0, 1, SHOP_DOOR_Z), WALK)
	await _travel(Vector3(0, 1, 100), DRIVE)
	var first_capture := await _capture(Vector3.LEFT, "first")
	await _travel(Vector3(0, 1, 180), DRIVE)
	await _frames(30)
	if not preload_all:
		_check(streamer.get_cell_state("shop") == LevelStreamer3D.CELL_UNLOADED, "shop still loaded 120 m away")
		_check(streamer.get_cell_content("shop") == null, "shop content still there")
		_check(streamer.get_cell_state("street_05") == LevelStreamer3D.CELL_UNLOADED, "street_05 still loaded")

	_phase("revisit")
	await _travel(Vector3(0, 1, 100), DRIVE)
	var second_capture := await _capture(Vector3.LEFT, "revisit")
	var difference := _image_difference(first_capture, second_capture)
	results["revisit_image_difference"] = difference
	_check(difference < 0.01, "street_05 looks different after its reload (mean difference %.4f)" % difference)
	await _travel(Vector3(0, 1, SHOP_DOOR_Z), DRIVE)
	shop = streamer.get_cell_content("shop")
	_check(shop != null and shop.get_node("Door").open, "door state lost after the reload")
	_check(shop != null and not shop.get_node("Light").on, "light switch state lost after the reload")
	await _travel(Vector3(16, 1, SHOP_DOOR_Z), WALK)
	_check_shop(shop, "revisit")
	await _travel(Vector3(0, 1, SHOP_DOOR_Z), WALK)

	_phase("boundary_crossing")
	# Between z = 95 and 105, street_07 (z 130..150) crosses its preload
	# distance and street_02 (z 30..50) its unload distance, 12 times each.
	# With the hysteresis each loads or unloads once, and none comes back.
	await _travel(Vector3(0, 1, 95), DRIVE)
	await _frames(30)
	var before := streamer.get_statistics()
	loads_in_phase.clear()
	unloads_in_phase.clear()
	for i in 12:
		await _travel(Vector3(0, 1, 105), FAST)
		await _travel(Vector3(0, 1, 95), FAST)
	var after := streamer.get_statistics()
	results["boundary_crossing_requests"] = after.requests - before.requests
	results["boundary_crossing_loads"] = loads_in_phase.duplicate()
	results["boundary_crossing_unloads"] = unloads_in_phase.duplicate()
	for id in loads_in_phase:
		_check(loads_in_phase[id] == 1 and not unloads_in_phase.has(id), "%s loaded %d times while crossing" % [id, loads_in_phase[id]])
	for id in unloads_in_phase:
		_check(unloads_in_phase[id] == 1 and not loads_in_phase.has(id), "%s unloaded %d times while crossing" % [id, unloads_in_phase[id]])
	if not preload_all:
		_check(after.requests - before.requests <= 1, "boundary crossing requested %d loads" % (after.requests - before.requests))

	_phase("teleport_thrash")
	# Back and forth faster than cells load: requests go stale and
	# instantiated scenes are dropped; the end state must still be right.
	checking = false
	for i in 40:
		player.global_position = Vector3(0, 1, 0.0 if i % 2 == 0 else 180.0)
		await _frames(2)
	var settle := await _until(func(): return streamer.is_position_ready(player.global_position), 10.0)
	_check(settle >= 0, "not ready after the thrash")
	await _frames(60)
	_check_consistency()
	checking = true

	_phase("cold_teleport")
	var destination := Vector3(0, 1, 400)
	events.erase("destination_ready")
	var prefetch_begin := Time.get_ticks_usec()
	streamer.prefetch_position(destination)
	var wait := await _until(func(): return events.has("destination_ready"), 10.0)
	_check(wait >= 0, "destination_ready never came")
	results["cold_teleport_ready_usec"] = Time.get_ticks_usec() - prefetch_begin
	_check(streamer.is_cell_ready("annex"), "the plaza's neighbor was not loaded with it")
	player.global_position = destination
	streamer.clear_prefetch()
	await get_tree().physics_frame
	_check_position()
	await _travel(Vector3(25, 1, 400), WALK)

	if not preload_all: # Everything is loaded already: nothing can fail.
		await _failed_destination(results)

	_phase("done")
	results["mode"] = "preload_all" if preload_all else ("streamed" if streamer.threaded_instantiation else "streamed_main_thread_instantiation")
	results["seconds"] = (Time.get_ticks_usec() - begin) / 1000000.0
	results["failures"] = failures
	results["not_ready_frames"] = not_ready_frames
	results["no_ground_frames"] = no_ground_frames
	results["statistics"] = streamer.get_statistics()
	results["frames"] = _frame_summary(func(i): return true)
	results["boundary_frames"] = _frame_summary(func(i): return frame_boundary[i] == 1)
	# Without the first cell's frames, which also compile the first shaders.
	results["boundary_frames_after_startup"] = _frame_summary(func(i): return frame_boundary[i] == 1 and phases[frame_phase[i]] != "startup")
	var per_phase := {}
	for p in phases.size():
		per_phase[phases[p]] = _frame_summary(func(i): return frame_phase[i] == p)
	results["phases"] = per_phase
	results["peak_rss_mib"] = _peak_rss_mib()
	results["peak_video_mem_mib"] = peak_video_mem / 1048576.0
	results["video_adapter"] = RenderingServer.get_video_adapter_name()
	results["activations"] = activations
	var file := FileAccess.open(results_path, FileAccess.WRITE)
	file.store_string(JSON.stringify(results, "\t"))
	file.close()
	print(JSON.stringify(results, "\t"))
	print("RESULT ", "PASS" if failures.is_empty() else "FAIL (%d)" % failures.size())
	get_tree().quit(0 if failures.is_empty() else 1)


func _failed_destination(results: Dictionary) -> void:
	_phase("failed_destination")
	var depot := streamer.get_cell("depot")
	var depot_scene := depot.scene_path
	depot.scene_path = "res://cells/missing_depot.tscn"
	events.erase("destination_ready")
	events.erase("failed_depot")
	var destination := Vector3(0, 1, 600)
	streamer.prefetch_position(destination)
	var failed := await _until(func(): return events.has("failed_depot"), 5.0)
	_check(failed >= 0, "no cell_failed for the missing scene")
	_check(not events.has("destination_ready"), "destination_ready although its cell failed")
	_check(not streamer.is_position_ready(destination), "failed destination reported ready")
	# The game keeps running at the plaza; the fixed scene loads on retry.
	await _travel(Vector3(25, 1, 405), WALK)
	depot.scene_path = depot_scene
	var recover := await _until(func(): return events.has("destination_ready"), streamer.retry_delay + 10.0)
	_check(recover >= 0, "the destination did not recover after its scene was fixed")
	results["failed_destination_recovery_usec"] = recover
	player.global_position = destination
	streamer.clear_prefetch()
	await get_tree().physics_frame
	_check_position()
	await _frames(30)


func _frame_summary(include: Callable) -> Dictionary:
	var values := []
	for i in frame_usec.size():
		if include.call(i):
			values.push_back(frame_usec[i])
	if values.is_empty():
		return {"count": 0}
	values.sort()
	var pick := func(q: float) -> float: return values[mini(int(q * values.size()), values.size() - 1)] / 1000.0
	return {"count": values.size(), "p50_ms": pick.call(0.5), "p95_ms": pick.call(0.95), "p99_ms": pick.call(0.99), "max_ms": values[-1] / 1000.0}


func _peak_rss_mib() -> float:
	# Linux: the high-water mark of the resident set. /proc files report a
	# length of 0, so FileAccess can't read them.
	var output := []
	OS.execute("grep", ["VmHWM", "/proc/%d/status" % OS.get_process_id()], output)
	if output.is_empty() or not output[0].begins_with("VmHWM:"):
		return -1.0
	return output[0].split(":")[1].strip_edges().split(" ")[0].to_int() / 1024.0
