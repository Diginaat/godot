extends Node3D
## Rigid body benchmark for stock Godot vs this fork
## (devtools/test/stock_compare.ps1). Drops RigidBody3D nodes into a walled
## yard and steps the simulation a fixed number of ticks. Run headless with
## --fixed-fps 60, so every frame is exactly one physics tick and the run
## is as fast as the engine can step. The physics engine comes from the
## project (the runner writes override.cfg per copy). Stock 4.7 API only.
##   godot --headless --fixed-fps 60 --path <copy> res://physics_bench.tscn -- --preset=crates_2k [--ticks=600]
## Prints one PHYSICS_BENCH line. Not a correctness test; the final state
## (bodies lost through the floor, mean height) shows the run was sane.

# name: bodies, shape mix (0 = boxes only, 1 = boxes, spheres, capsules), yard size (m)
const PRESETS := {
	"crates_2k": [2000, 0, 40.0],
	"mixed_8k": [8000, 1, 60.0],
	"extreme_30k": [30000, 1, 100.0],
}

var _preset := "crates_2k"
var _ticks := 600
var _spec: Array
var _bodies: Array[RigidBody3D] = []
var _tick := 0
var _last_usec := 0
var _build_ms := 0
# Wall time per frame; with --fixed-fps 60 and headless, one frame is one physics tick.
var _wall: Array[float] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--preset="):
			_preset = arg.get_slice("=", 1)
		elif arg.begins_with("--ticks="):
			_ticks = arg.get_slice("=", 1).to_int()
	if not PRESETS.has(_preset):
		print("PHYSICS_BENCH_FAIL: unknown preset %s" % _preset)
		get_tree().quit(1)
		return
	_spec = PRESETS[_preset]
	var started := Time.get_ticks_msec()
	_build()
	_build_ms = Time.get_ticks_msec() - started
	_last_usec = Time.get_ticks_usec()


func _static_box(p_pos: Vector3, p_size: Vector3) -> void:
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = p_size
	shape.shape = box
	body.add_child(shape)
	body.position = p_pos
	add_child(body)


func _build() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 777
	var count: int = _spec[0]
	var mix: int = _spec[1]
	var yard: float = _spec[2]
	_static_box(Vector3(0, -0.5, 0), Vector3(yard + 20.0, 1.0, yard + 20.0))
	for side in 4:
		var a := side * PI * 0.5
		_static_box(Vector3(cos(a), 0.0, sin(a)) * (yard * 0.5 + 0.5) + Vector3(0, 4, 0), Vector3(1.0 if side % 2 == 0 else yard + 2.0, 8.0, yard + 2.0 if side % 2 == 0 else 1.0))
	# A few ramps and blocks inside, so bodies pile and slide instead of only stacking.
	for i in 12:
		_static_box(Vector3(rng.randf_range(-0.35, 0.35) * yard, 0.5, rng.randf_range(-0.35, 0.35) * yard), Vector3(rng.randf_range(2, 6), 1.0 + rng.randf() * 2.0, rng.randf_range(2, 6)))

	var box := BoxShape3D.new()
	box.size = Vector3(0.8, 0.8, 0.8)
	var sphere := SphereShape3D.new()
	sphere.radius = 0.4
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.25
	capsule.height = 1.0
	var shapes: Array[Shape3D] = [box, sphere, capsule]
	var per_layer := int(pow(yard * 0.9 / 1.0, 2.0))
	var row := int(sqrt(float(per_layer)))
	for i in count:
		var body := RigidBody3D.new()
		var collision := CollisionShape3D.new()
		collision.shape = shapes[0] if mix == 0 else shapes[i % 3]
		body.add_child(collision)
		var layer := i / per_layer
		var k := i % per_layer
		body.position = Vector3((k % row - row * 0.5) * 1.0 + rng.randf_range(-0.1, 0.1), 2.0 + layer * 1.1, (k / row - row * 0.5) * 1.0 + rng.randf_range(-0.1, 0.1))
		body.rotation = Vector3(rng.randf() * TAU, rng.randf() * TAU, rng.randf() * TAU)
		add_child(body)
		_bodies.append(body)


func _process(_delta: float) -> void:
	_tick += 1
	var now := Time.get_ticks_usec()
	_wall.append((now - _last_usec) / 1000.0)
	_last_usec = now
	if _tick >= _ticks:
		set_process(false)
		_report()
		get_tree().quit()


func _pct(p_values: Array[float], p_q: float) -> float:
	var s := p_values.duplicate()
	s.sort()
	return s[clampi(int(round((s.size() - 1) * p_q)), 0, s.size() - 1)]


func _mean(p_values: Array[float], p_from: int, p_to: int) -> float:
	var total := 0.0
	for i in range(p_from, p_to):
		total += p_values[i]
	return total / maxf(1.0, p_to - p_from)


func _report() -> void:
	var lost := 0
	var sleeping := 0
	var height := 0.0
	for body in _bodies:
		if body.position.y < -5.0:
			lost += 1
		if body.sleeping:
			sleeping += 1
		height += body.position.y
	var engine_name = ProjectSettings.get_setting("physics/3d/physics_engine", "DEFAULT")
	# The fork's PhysX singleton says whether GPU dynamics actually run; stock has no such class.
	var gpu = "n/a"
	if ClassDB.class_exists("PhysX"):
		gpu = ClassDB.class_call_static("PhysX", "is_gpu_available")
	var half := _wall.size() / 2
	# The first ticks carry body creation and the first broad phase; skip 5.
	var timed := _wall.slice(5)
	print("PHYSICS_BENCH: preset=%s bodies=%d engine=%s physics=%s gpu_dynamics=%s build_ms=%d ticks=%d tick_mean_ms=%.3f tick_p50=%.3f tick_p95=%.3f tick_max=%.3f fall_mean_ms=%.3f settle_mean_ms=%.3f total_s=%.2f lost=%d sleeping=%d mean_y=%.2f" % [
		_preset, _bodies.size(), Engine.get_version_info().string, engine_name, gpu, _build_ms, _wall.size(),
		_mean(_wall, 5, _wall.size()), _pct(timed, 0.5), _pct(timed, 0.95), timed.max(),
		_mean(_wall, 5, half), _mean(_wall, half, _wall.size()),
		_mean(_wall, 5, _wall.size()) * (_wall.size() - 5) / 1000.0, lost, sleeping, height / maxf(1, _bodies.size())])
