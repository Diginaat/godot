extends Node
## Adaptive variable rate shading (Viewport.VRS_ADAPTIVE): viewport GPU time
## and image difference against VRS off, in a profiled scene with a fixed
## camera. Prints VRS_ADAPTIVE lines for three thresholds and
## VRS_ADAPTIVE_TEST_PASS when the default threshold (0.1) keeps the image at
## least 45 dB PSNR from VRS off and costs no GPU time.
##
## User arguments: --vrs-scene=res://... (default forest_map.tscn),
## --vrs-settle=N (frames before measuring, default 600); everything else goes
## --vrs-shots=DIR (save the images as PNG); everything else goes to the
## profiled scene (forest_map: --view-time=T fixes the camera). Time is frozen
## after settling (Engine.time_scale 0, which also stops shader TIME), so the
## frames compared differ only by the shading rate.

const THRESHOLDS: Array[float] = [0.05, 0.1, 0.2]
const MEASURE_FRAMES := 200
const DEFAULT_THRESHOLD := 0.1
const MIN_PSNR := 45.0


func _measure() -> float:
	var rid := get_viewport().get_viewport_rid()
	for i in 30:
		await get_tree().process_frame
	var total := 0.0
	for i in MEASURE_FRAMES:
		await get_tree().process_frame
		total += RenderingServer.viewport_get_measured_render_time_gpu(rid)
	return total / MEASURE_FRAMES


static func _psnr(a: Image, b: Image) -> float:
	var se := 0.0
	var n := 0
	for y in range(0, a.get_height(), 2):
		for x in range(0, a.get_width(), 2):
			var p := a.get_pixel(x, y)
			var q := b.get_pixel(x, y)
			se += (p.r - q.r) * (p.r - q.r) + (p.g - q.g) * (p.g - q.g) + (p.b - q.b) * (p.b - q.b)
			n += 3
	return 10.0 * log(1.0 / maxf(se / n, 1e-12)) / log(10.0)


func _ready() -> void:
	var scene_path := "res://forest_map.tscn"
	var settle := 600
	var shots := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--vrs-scene="):
			scene_path = arg.get_slice("=", 1)
		elif arg.begins_with("--vrs-settle="):
			settle = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--vrs-shots="):
			shots = arg.get_slice("=", 1)
	add_child(load(scene_path).instantiate())
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var viewport := get_viewport()
	RenderingServer.viewport_set_measure_render_time(viewport.get_viewport_rid(), true)
	for i in settle:
		await get_tree().process_frame
	Engine.time_scale = 0.0

	viewport.vrs_mode = Viewport.VRS_DISABLED
	var off := await _measure()
	var image_off := viewport.get_texture().get_image()
	var off_again := await _measure()
	var noise := _psnr(image_off, viewport.get_texture().get_image())
	if not shots.is_empty():
		image_off.save_png(shots.path_join("vrs_off.png"))
	print("VRS_ADAPTIVE: off %.3f ms, again %.3f ms, frame-to-frame PSNR %.1f dB" % [off, off_again, noise])
	var failed := false
	for threshold in THRESHOLDS:
		ProjectSettings.set_setting("rendering/vrs/adaptive_threshold", threshold)
		viewport.vrs_mode = Viewport.VRS_ADAPTIVE
		var on := await _measure()
		var image_on := viewport.get_texture().get_image()
		var psnr := _psnr(image_off, image_on)
		if not shots.is_empty():
			image_on.save_png(shots.path_join("vrs_adaptive_%.2f.png" % threshold))
		viewport.vrs_mode = Viewport.VRS_DISABLED
		var reference := await _measure()
		print("VRS_ADAPTIVE: threshold %.2f: %.3f ms against %.3f ms off (%.0f%%), PSNR %.1f dB" % [threshold, on, reference, 100.0 * on / reference, psnr])
		if is_equal_approx(threshold, DEFAULT_THRESHOLD):
			failed = psnr < MIN_PSNR or on > reference * 1.02
	ProjectSettings.set_setting("rendering/vrs/adaptive_threshold", DEFAULT_THRESHOLD)
	print("VRS_ADAPTIVE_TEST_FAIL" if failed else "VRS_ADAPTIVE_TEST_PASS")
	get_tree().quit(1 if failed else 0)
