extends Node3D
## GPU path for eligible instances (devtools/test/gpu_instances.ps1). Turns on
## rendering/geometry/gpu_instances with its validation, then changes the
## scene in phases: moves, layers the camera does not see, hidden nodes,
## materials that leave the path (transparent) and come back, transparency,
## visibility ranges, meshes with index LODs and LOD biases, frees and adds,
## nodes replaced and materials swapped every frame (no bucket rebuilds),
## an occluder (the GPU path draws what the scene cull found visible),
## TAA and FSR 2 (a motion pass), compared through their motion vectors, a second view of the same world and a
## view of another world. Every 30 frames the engine compares what the GPU
## path drew with what the CPU cull found visible (GPU_INSTANCES_CHECK). After
## each phase the image with the GPU path is compared with the image without
## it (GPU_INSTANCES_AB); they must be equal. With `-- --self-test` the engine
## leaves one draw bucket out, so both checks have to report a difference.

const PHASE_FRAMES := 60

var _nodes: Array[MeshInstance3D] = []
var _meshes: Array[Mesh] = []
var _materials: Array[StandardMaterial3D] = []
var _transparent := StandardMaterial3D.new()
var _moving := 0
var _flipping := 0 # Nodes that alternate between two positions every frame.
var _churn := 0 # Nodes replaced, and materials swapped, every frame.
var _camera: Camera3D
var _velocity := VelocityGrab.new()


var _frame := 0
var _ab_failures := 0


func _ready() -> void:
	var self_test := OS.get_cmdline_user_args().has("--self-test")
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", true)
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/validate", 2 if self_test else 1)
	_meshes.append(BoxMesh.new())
	var sphere := SphereMesh.new()
	sphere.radial_segments = 16
	sphere.rings = 8
	_meshes.append(sphere)
	var cylinder := CylinderMesh.new()
	cylinder.radial_segments = 12
	_meshes.append(cylinder)
	for i in 4:
		var material := StandardMaterial3D.new()
		material.albedo_color = Color.from_hsv(float(i) * 0.25, 0.6, 0.9)
		material.roughness = 0.3 + 0.15 * float(i)
		_materials.append(material)
	_transparent.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_transparent.albedo_color = Color(1, 1, 1, 0.5)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	add_child(sun)
	var omni := OmniLight3D.new()
	omni.position = Vector3(5, 3, 5)
	omni.omni_range = 12
	omni.light_color = Color(1, 0.6, 0.3)
	add_child(omni)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 30, 45)
	camera.cull_mask = 1 # Nodes moved to layer 2 are not visible.
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	_camera = camera
	_run.call_deferred(self_test)


func _add(count: int) -> void:
	for i in count:
		var node := MeshInstance3D.new()
		var index := _nodes.size()
		node.mesh = _meshes[index % _meshes.size()]
		node.material_override = _materials[(index / 3) % _materials.size()]
		node.position = Vector3(float(index % 60) - 30.0, 0.5, float(index / 60) - 30.0)
		node.rotation.y = float(index) * 0.37
		node.scale = Vector3.ONE * (0.6 + 0.4 * float(index % 5) / 4.0)
		add_child(node)
		_nodes.append(node)


func _process(_delta: float) -> void:
	_frame += 1
	for i in mini(_moving, _nodes.size()):
		var node := _nodes[i * 7 % _nodes.size()]
		node.position.y = 0.5 + 0.5 * sin(float(_frame) * 0.1 + float(i))
	for k in _churn:
		var index := (_frame * 7 + k * 131) % _nodes.size()
		var old := _nodes[index]
		var node := MeshInstance3D.new()
		node.mesh = old.mesh
		node.material_override = old.material_override
		node.transform = old.transform
		node.lod_bias = old.lod_bias
		node.layers = old.layers
		add_child(node)
		old.queue_free()
		_nodes[index] = node
		var swapped := _nodes[(_frame * 11 + k * 37) % _nodes.size()]
		if _materials.has(swapped.material_override):
			swapped.material_override = _materials[(_frame + k) % _materials.size()]
	# Positions that depend only on the frame's parity, so that motion vectors
	# repeat exactly. Lifted clear of the grid: where a moving and a resting
	# node of one draw bucket intersect, equal depths let the draw order pick
	# the motion vector, and the GPU path orders a bucket differently.
	var shift := 0.25 if Engine.get_process_frames() % 2 == 0 else 0.0
	for i in mini(_flipping, _nodes.size()):
		var index := i * 3 % _nodes.size()
		_nodes[index].position = Vector3(float(index % 60) - 30.0 + shift, 3.0, float(index / 60) - 30.0)


func _wait(frames: int) -> void:
	for frame in frames:
		await RenderingServer.frame_post_draw


func _capture() -> Image:
	await _wait(3)
	return get_viewport().get_texture().get_image()


# Same frame content with and without the GPU path: nothing may move.
func _compare(label: String) -> void:
	var moving := _moving
	_moving = 0
	var with_gpu := await _capture()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", false)
	var without_gpu := await _capture()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", true)
	await _wait(3)
	_moving = moving
	if with_gpu.get_data() == without_gpu.get_data():
		print("GPU_INSTANCES_AB: equal %s %dx%d" % [label, with_gpu.get_width(), with_gpu.get_height()])
		return
	var differing := 0
	var largest := 0.0
	for y in with_gpu.get_height():
		for x in with_gpu.get_width():
			var a := with_gpu.get_pixel(x, y)
			var b := without_gpu.get_pixel(x, y)
			if a != b:
				differing += 1
				largest = maxf(largest, maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b))))
	_ab_failures += 1
	printerr("GPU_INSTANCES_AB: differ %s: %d pixels, largest channel difference %.4f" % [label, differing, largest])


func _run(self_test: bool) -> void:
	_add(3000)
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: added %d" % _nodes.size())
	await _compare("static")
	if self_test:
		print("GPU_INSTANCES_TEST_DONE: self-test, %d image differences" % _ab_failures)
		get_tree().quit(0)
		return
	_moving = 200
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: moving 200")
	await _compare("moved")
	for i in 150:
		_nodes[i * 13 % _nodes.size()].layers = 2
	for i in 100:
		_nodes[i * 17 % _nodes.size() + 1].visible = false
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: layers and hidden")
	await _compare("layers and hidden")
	for i in 60:
		_nodes[i * 19 % _nodes.size()].material_override = _transparent
	for i in 40:
		_nodes[i * 23 % _nodes.size() + 2].transparency = 0.5
	for i in 40:
		_nodes[i * 29 % _nodes.size() + 3].visibility_range_end = 30.0
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: transparent, transparency, visibility range")
	await _compare("ineligible")
	for i in 60:
		_nodes[i * 19 % _nodes.size()].material_override = _materials[i % _materials.size()]
	for i in 40:
		_nodes[i * 23 % _nodes.size() + 2].transparency = 0.0
	for i in 100:
		_nodes[i * 17 % _nodes.size() + 1].visible = true
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: back to eligible")
	await _compare("eligible again")
	var lod_mesh := _lod_mesh()
	for i in range(0, _nodes.size(), 2):
		_nodes[i].mesh = lod_mesh
	# Biases from coarser to much finer, so that every index LOD is drawn.
	for i in 1000:
		_nodes[i * 2 % _nodes.size()].lod_bias = [0.5, 4.0, 16.0, 64.0, 256.0][i % 5]
	# Separate shadow meshes, which the depth pre-pass draws.
	var shadowed := _shadowed_meshes()
	for i in range(1, _nodes.size(), 4):
		_nodes[i].mesh = shadowed[(i / 4) % shadowed.size()]
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: index LODs and shadow meshes")
	await _compare("index LODs and shadow meshes")
	_moving = 0
	for i in 300:
		var node: MeshInstance3D = _nodes.pop_back()
		node.queue_free()
	await _wait(PHASE_FRAMES)
	_add(600)
	_moving = 200
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: freed 300, added 600, %d" % _nodes.size())
	await _compare("freed and added")
	# Nodes come and go and change materials every frame: their surfaces join
	# and leave buckets without a rebuild.
	print("GPU_INSTANCES_PHASE: churn start")
	_churn = 20
	await _wait(PHASE_FRAMES)
	_churn = 0
	print("GPU_INSTANCES_PHASE: churn, 20 nodes replaced per frame")
	await _compare("churn")
	# Occlusion culling: the scene cull leaves out what an occluder covers, and
	# the GPU path draws what the cull found visible.
	await _wait(10) # Render info of the GPU path arrives a few frames late.
	var viewport_rid := get_viewport().get_viewport_rid()
	var objects_before := RenderingServer.viewport_get_render_info(viewport_rid, RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME)
	get_viewport().use_occlusion_culling = true
	var occluder := OccluderInstance3D.new()
	var box := BoxOccluder3D.new()
	box.size = Vector3(40.0, 8.0, 1.0)
	occluder.occluder = box
	occluder.position = Vector3(0.0, 4.0, 14.0)
	add_child(occluder)
	await _wait(PHASE_FRAMES)
	var objects_after := RenderingServer.viewport_get_render_info(viewport_rid, RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME)
	print("GPU_INSTANCES_PHASE: occluder, objects %d before, %d with it" % [objects_before, objects_after])
	await _compare("occluder")
	occluder.queue_free()
	get_viewport().use_occlusion_culling = false
	await _compare_motion(false)
	await _compare_motion(true)
	_add_views()
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: second view and second world")
	await _compare("more views")
	print("GPU_INSTANCES_TEST_DONE: %d nodes, %d image differences" % [_nodes.size(), _ab_failures])
	get_tree().quit(0)


# TAA writes motion vectors in the opaque pass; FSR 2 gives the view a motion
# pass, where moved instances are drawn with motion vectors. Nodes alternate
# between two positions each frame; the velocity buffers of four frames, with
# the same jitter phase, must be equal with and without the GPU path.
func _compare_motion(motion_pass: bool) -> void:
	var label := "FSR 2" if motion_pass else "TAA"
	_moving = 0
	_flipping = 300
	_camera.compositor = Compositor.new()
	_camera.compositor.compositor_effects = [_velocity]
	if motion_pass:
		get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
	else:
		get_viewport().use_taa = true
	await _wait(PHASE_FRAMES)
	print("GPU_INSTANCES_PHASE: %s with %d nodes moving every frame" % [label, _flipping])
	var with_gpu := await _grab_velocity()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", false)
	await _wait(6)
	var without_gpu := await _grab_velocity()
	ProjectSettings.set_setting("rendering/geometry/gpu_instances/enabled", true)
	await _wait(6)
	get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	get_viewport().use_taa = false
	_camera.compositor = null
	for i in mini(_flipping, _nodes.size()):
		var index := i * 3 % _nodes.size()
		_nodes[index].position = Vector3(float(index % 60) - 30.0, 0.5, float(index / 60) - 30.0)
	_flipping = 0
	await _wait(3)
	_velocity.free_resources()
	var moving_pixels := 0
	if not with_gpu.is_empty():
		var data := with_gpu[0]
		for offset in range(0, data.size(), 4):
			var x := data.decode_half(offset)
			var y := data.decode_half(offset + 2)
			if (x != -1.0 and x != 0.0) or (y != -1.0 and y != 0.0):
				moving_pixels += 1
	# Compared as numbers: the GPU path's pipelines are other specializations,
	# and a compiler may give a zero vector the other sign (D3D12 does).
	var pixels: Array[int] = []
	for frame in mini(with_gpu.size(), without_gpu.size()):
		var count := 0
		var a := with_gpu[frame]
		var b := without_gpu[frame]
		if a != b:
			for offset in range(0, mini(a.size(), b.size()), 2):
				if a.decode_u16(offset) != b.decode_u16(offset) and a.decode_half(offset) != b.decode_half(offset):
					if count == 0:
						var pixel := offset / 4
						var width := int(get_viewport().get_visible_rect().size.x)
						printerr("GPU_INSTANCES_MV: %s frame %d pixel (%d, %d): %.6f with, %.6f without the GPU path" % [label, frame, pixel % width, pixel / width, a.decode_half(offset), b.decode_half(offset)])
					count += 1
		pixels.append(count)
	if with_gpu.size() == 4 and without_gpu.size() == 4 and pixels.max() == 0 and moving_pixels > 0:
		print("GPU_INSTANCES_MV: equal %s, 4 frames, %d pixels with motion" % [label, moving_pixels])
		return
	_ab_failures += 1
	printerr("GPU_INSTANCES_MV: differ %s: %d and %d frames read, differing values per frame %s, %d pixels with motion" % [label, with_gpu.size(), without_gpu.size(), str(pixels), moving_pixels])


func _grab_velocity() -> Array[PackedByteArray]:
	while Engine.get_frames_drawn() % 16 != 0:
		await RenderingServer.frame_post_draw
	_velocity.take()
	_velocity.remaining = 4
	await _wait(10)
	return _velocity.take()


# A dense sphere with generated index LODs, so that the LOD chosen per instance
# changes with distance across the grid.
func _lod_mesh() -> Mesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = 96
	sphere.rings = 48
	var importer := ImporterMesh.new()
	importer.add_surface(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	importer.generate_lods(25.0, 60.0, [])
	print("GPU_INSTANCES_PHASE: LOD mesh with %d index LODs" % importer.get_surface_lod_count(0))
	return importer.get_mesh()


# Meshes with a separate shadow mesh: one whose shadow mesh has other index
# counts (a coarser sphere), and one with index LODs on both.
func _shadowed_meshes() -> Array[Mesh]:
	var fine := SphereMesh.new()
	fine.radial_segments = 32
	fine.rings = 16
	var coarse := SphereMesh.new()
	coarse.radial_segments = 10
	coarse.rings = 5
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, fine.get_mesh_arrays())
	var shadow := ArrayMesh.new()
	shadow.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, coarse.get_mesh_arrays())
	mesh.shadow_mesh = shadow
	var lod_mesh: ArrayMesh = _lod_mesh()
	lod_mesh.shadow_mesh = _lod_mesh()
	return [mesh, lod_mesh]


func _add_views() -> void:
	# A second view of the same world, and a view of a world of its own.
	var same := SubViewport.new()
	same.size = Vector2i(320, 180)
	same.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	same.world_3d = get_world_3d()
	var camera := Camera3D.new()
	camera.position = Vector3(20, 6, 0)
	same.add_child(camera)
	add_child(same)
	camera.look_at(Vector3(10, 0, -20))
	camera.current = true

	var other := SubViewport.new()
	other.size = Vector2i(320, 180)
	other.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	other.own_world_3d = true
	add_child(other)
	var other_camera := Camera3D.new()
	other_camera.position = Vector3(0, 8, 12)
	other.add_child(other_camera)
	other_camera.look_at(Vector3.ZERO)
	other_camera.current = true
	for i in 200:
		var node := MeshInstance3D.new()
		node.mesh = _meshes[i % _meshes.size()]
		node.material_override = _materials[i % _materials.size()]
		node.position = Vector3(float(i % 20) - 10.0, 0.5, float(i / 20) - 10.0)
		other.add_child(node)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-60, 20, 0)
	other.add_child(light)


# Copies the velocity buffer after the opaque and motion passes (it cannot be
# read back directly) and reads the copy back.
class VelocityGrab extends CompositorEffect:
	const COPY := """
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D velocity;
layout(set = 0, binding = 1, std430) restrict writeonly buffer Copy {
	uint data[];
} copy;
layout(push_constant, std430) uniform Params {
	ivec2 size;
	ivec2 pad;
} params;
void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, params.size))) {
		return;
	}
	copy.data[pixel.y * params.size.x + pixel.x] = packHalf2x16(texelFetch(velocity, pixel, 0).xy);
}
"""
	var remaining := 0
	var _results: Array[PackedByteArray] = []
	var _mutex := Mutex.new()
	var _rd: RenderingDevice
	var _shader: RID
	var _pipeline: RID
	var _sampler: RID
	var _buffer: RID
	var _buffer_size := Vector2i()

	func _init() -> void:
		effect_callback_type = EFFECT_CALLBACK_TYPE_POST_OPAQUE
		needs_motion_vectors = true

	func _render_callback(_type: int, render_data: RenderData) -> void:
		if remaining <= 0:
			return
		remaining -= 1
		if not _rd:
			_rd = RenderingServer.get_rendering_device()
			var source := RDShaderSource.new()
			source.source_compute = COPY
			_shader = _rd.shader_create_from_spirv(_rd.shader_compile_spirv_from_source(source))
			_pipeline = _rd.compute_pipeline_create(_shader)
			_sampler = _rd.sampler_create(RDSamplerState.new())
		var buffers := render_data.get_render_scene_buffers() as RenderSceneBuffersRD
		var size := buffers.get_internal_size()
		if size != _buffer_size:
			if _buffer.is_valid():
				_rd.free_rid(_buffer)
			_buffer = _rd.storage_buffer_create(size.x * size.y * 4)
			_buffer_size = size
		var velocity := RDUniform.new()
		velocity.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
		velocity.binding = 0
		velocity.add_id(_sampler)
		velocity.add_id(buffers.get_velocity_texture())
		var copy := RDUniform.new()
		copy.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
		copy.binding = 1
		copy.add_id(_buffer)
		var uniform_set := _rd.uniform_set_create([velocity, copy], _shader, 0)
		var params := PackedInt32Array([size.x, size.y, 0, 0]).to_byte_array()
		var list := _rd.compute_list_begin()
		_rd.compute_list_bind_compute_pipeline(list, _pipeline)
		_rd.compute_list_bind_uniform_set(list, uniform_set, 0)
		_rd.compute_list_set_push_constant(list, params, params.size())
		_rd.compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1)
		_rd.compute_list_end()
		_rd.free_rid(uniform_set)
		_rd.buffer_get_data_async(_buffer, _received)

	func _received(data: PackedByteArray) -> void:
		_mutex.lock()
		_results.append(data)
		_mutex.unlock()

	func take() -> Array[PackedByteArray]:
		_mutex.lock()
		var taken := _results.duplicate()
		_results.clear()
		_mutex.unlock()
		return taken

	func free_resources() -> void:
		if not _rd:
			return
		for resource in [_buffer, _sampler, _pipeline, _shader]:
			if resource.is_valid():
				_rd.free_rid(resource)
		_buffer = RID()
		_buffer_size = Vector2i()
		_rd = null
