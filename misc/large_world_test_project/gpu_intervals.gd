extends Node
## GPU time between consecutive timestamps over the whole frame, averaged.
## Each line is charged to the label that opens the interval, so a slot holds
## all GPU work up to the next timestamp. Unlike the viewport's render time,
## it includes work recorded before the viewport (for example
## call_on_render_thread() compute). Run through devtools/test/gpu_intervals.ps1
## (with --gpu-profile, so the engine's own RENDER_TIMESTAMP labels record too).
##
## User arguments: --prof-scene=res://... (default forest_map.tscn),
## --prof-start=N (frames to skip, default 600), --prof-frames=N (default 300);
## everything else goes to the profiled scene.

var _sums := {}
var _order: Array[String] = []
var _total := 0.0
var _frame := 0
var _start := 600
var _frames := 300


func _ready() -> void:
	var scene_path := "res://forest_map.tscn"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--prof-scene="):
			scene_path = arg.get_slice("=", 1)
		elif arg.begins_with("--prof-start="):
			_start = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--prof-frames="):
			_frames = maxi(1, arg.get_slice("=", 1).to_int())
	add_child(load(scene_path).instantiate())


func _process(_delta: float) -> void:
	_frame += 1
	if _frame <= _start:
		return
	var rd := RenderingServer.get_rendering_device()
	var count := rd.get_captured_timestamps_count()
	if count < 2:
		push_error("GPU_INTERVALS: no timestamps captured")
		get_tree().quit(1)
		return
	for i in range(count - 1):
		var label := rd.get_captured_timestamp_name(i)
		if not _sums.has(label):
			_sums[label] = 0.0
			_order.append(label)
		_sums[label] += (rd.get_captured_timestamp_gpu_time(i + 1) - rd.get_captured_timestamp_gpu_time(i)) / 1000000.0
	_total += (rd.get_captured_timestamp_gpu_time(count - 1) - rd.get_captured_timestamp_gpu_time(0)) / 1000000.0
	if _frame == _start + _frames:
		for label in _order:
			var ms: float = _sums[label] / _frames
			if ms >= 0.01:
				print("GPU_INTERVAL: %7.3f ms  %s" % [ms, label])
		print("GPU_INTERVAL_TOTAL: %.3f ms over %d frames" % [_total / _frames, _frames])
		get_tree().quit()
