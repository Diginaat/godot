extends Node3D
## More visible omni lights than rendering/limits/cluster_builder/max_clustered_elements
## (512 here): the lights left out must be the ones farthest from the camera,
## not the last ones the cull lists. A red light near the camera is added
## first, then 520 dim lights far away, then a green light near the camera:
## whatever order the cull lists them in, one near light is at its end. The
## floor under both must take their colour. Prints LIGHT_CAP_TEST_PASS.

const FAR_LIGHTS := 520


func _ready() -> void:
	get_window().size = Vector2i(480, 270)
	get_viewport().vrs_mode = Viewport.VRS_DISABLED
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0, 0, 0)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.02, 0.02, 0.02)
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var floor_node := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(400, 400)
	floor_node.mesh = plane
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.8, 0.8, 0.8)
	floor_node.material_override = material
	add_child(floor_node)
	_near(Vector3(-2.0, 1.0, -5.0), Color(1.0, 0.1, 0.1))
	for i in FAR_LIGHTS:
		var light := OmniLight3D.new()
		light.position = Vector3(float(i % 40 - 20) * 4.0, 1.0, -120.0 - float(i / 40) * 6.0)
		light.omni_range = 2.0
		light.light_energy = 0.2
		light.light_color = Color(0.2, 0.2, 1.0)
		add_child(light)
	_near(Vector3(2.0, 1.0, -5.0), Color(0.1, 1.0, 0.1))
	var camera := Camera3D.new()
	camera.position = Vector3(0, 3.0, 0)
	camera.far = 300.0
	add_child(camera)
	camera.look_at(Vector3(0, 0, -20))
	camera.current = true
	_check.call_deferred(camera)


func _near(p_position: Vector3, p_color: Color) -> void:
	var light := OmniLight3D.new()
	light.position = p_position
	light.omni_range = 1.8
	light.light_energy = 6.0
	light.light_color = p_color
	add_child(light)


func _check(camera: Camera3D) -> void:
	for frame in 30:
		await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	var red := image.get_pixelv(Vector2i(camera.unproject_position(Vector3(-2.0, 0, -5.0))))
	var green := image.get_pixelv(Vector2i(camera.unproject_position(Vector3(2.0, 0, -5.0))))
	if red.r < 0.3 or red.r < red.g * 3.0 or green.g < 0.3 or green.g < green.r * 3.0:
		printerr("LIGHT_CAP_TEST_FAIL: a near light is missing: red %s, green %s" % [red, green])
		get_tree().quit(1)
		return
	print("LIGHT_CAP_TEST_PASS: %d omni lights visible, both near lights drawn (red %s, green %s)" % [FAR_LIGHTS + 2, red, green])
	get_tree().quit(0)
