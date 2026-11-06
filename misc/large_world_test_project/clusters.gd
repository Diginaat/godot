extends Node
## Native cooker, hierarchy cuts, boundary topology and resource serialization.

var _failures: Array[String] = []


func _edge(a: int, b: int) -> Vector2i:
	return Vector2i(mini(a, b), maxi(a, b))


func _boundary(indices: PackedInt32Array) -> Dictionary:
	var counts := {}
	for triangle in indices.size() / 3:
		for e in 3:
			var edge := _edge(indices[triangle * 3 + e], indices[triangle * 3 + (e + 1) % 3])
			counts[edge] = int(counts.get(edge, 0)) + 1
	var boundary := {}
	for edge in counts:
		if counts[edge] == 1:
			boundary[edge] = true
		elif counts[edge] != 2:
			_failures.append("non-manifold cut edge")
	return boundary


func _ready() -> void:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uv := PackedVector2Array()
	var source_indices := PackedInt32Array()
	const EDGE := 48
	for z in EDGE + 1:
		for x in EDGE + 1:
			vertices.append(Vector3(x, sin(float(x) * 0.2) * cos(float(z) * 0.2), z))
			normals.append(Vector3.UP)
			uv.append(Vector2(x, z) / EDGE)
	for z in EDGE:
		for x in EDGE:
			var a := z * (EDGE + 1) + x
			source_indices.append_array(PackedInt32Array([a, a + EDGE + 1, a + 1, a + 1, a + EDGE + 1, a + EDGE + 2]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uv
	arrays[Mesh.ARRAY_INDEX] = source_indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var before := mesh.surface_get_arrays(0)
	var data := MeshClusterData.new()
	var error := data.generate(mesh)
	if error != OK:
		_failures.append("native cooker returned %d" % error)
		_finish()
		return
	if mesh.surface_get_arrays(0) != before:
		_failures.append("cooker mutated source vertex/index attributes")
	var surface := data.get_surface_data(0)
	var groups: PackedInt32Array = surface.groups
	var clusters: PackedInt32Array = surface.clusters
	var indices: PackedInt32Array = surface.indices
	var leaves := PackedInt32Array()
	var leaf_groups: Array[int] = []
	for group in groups.size() / 5:
		if groups[group * 5 + 3] == 0:
			leaf_groups.append(group)
			for c in groups[group * 5 + 1]:
				var cluster := groups[group * 5] + c
				leaves.append_array(indices.slice(clusters[cluster * 3], clusters[cluster * 3] + clusters[cluster * 3 + 1]))
	if leaves.size() != source_indices.size():
		_failures.append("leaf triangle count differs from source")
	var expected_boundary := _boundary(source_indices)
	if _boundary(leaves) != expected_boundary:
		_failures.append("leaf boundary differs from source")
	var previous_triangle_count := source_indices.size()
	for threshold in [0.0, 0.1, 1.0, 10.0, 1000000.0]:
		var selected := data.select_clusters(0, Vector3(12, 10, 12), 500.0, threshold)
		var selected_groups := {}
		var cut := PackedInt32Array()
		for cluster in selected:
			selected_groups[clusters[cluster * 3 + 2]] = int(selected_groups.get(clusters[cluster * 3 + 2], 0)) + 1
			cut.append_array(indices.slice(clusters[cluster * 3], clusters[cluster * 3] + clusters[cluster * 3 + 1]))
		for leaf in leaf_groups:
			var hits := 0
			var group := leaf
			while group != -1:
				if selected_groups.has(group):
					hits += 1
					if selected_groups[group] != groups[group * 5 + 1]:
						_failures.append("selected only part of a simplification group")
				group = groups[group * 5 + 4]
			if hits != 1:
				_failures.append("cut has a hole or overlapping parent/child")
		if _boundary(cut) != expected_boundary:
			_failures.append("CLOD cut introduces a crack or changes external boundary")
		if cut.size() > previous_triangle_count:
			_failures.append("coarser threshold increased triangle count")
		previous_triangle_count = cut.size()
	if previous_triangle_count >= source_indices.size():
		_failures.append("cooker did not generate a simplified representation")
	var extreme := data.select_clusters(0, Vector3(1.0e30, 1.0e30, 1.0e30), 1.0e30, 1.0)
	if extreme.is_empty():
		_failures.append("extreme finite projection inputs removed the whole hierarchy")
	var saved := ResourceSaver.save(data, "user://cluster_test.tres")
	var loaded := ResourceLoader.load("user://cluster_test.tres", "MeshClusterData", ResourceLoader.CACHE_MODE_IGNORE) as MeshClusterData
	if saved != OK or loaded == null or loaded.get_data() != data.get_data():
		_failures.append("cluster resource round trip failed")
	var broken := data.get_data()
	var bad_surface: Dictionary = broken[0]
	var bad_groups: PackedInt32Array = bad_surface.groups
	bad_groups[4] = 0 # A leaf cannot be its own parent.
	bad_surface.groups = bad_groups
	broken[0] = bad_surface
	if data.set_data_checked(broken) != ERR_INVALID_DATA:
		_failures.append("cyclic hierarchy was accepted")
	broken = data.get_data()
	bad_surface = broken[0]
	var bad_indices: PackedInt32Array = bad_surface.indices
	bad_indices[0] = vertices.size()
	bad_surface.indices = bad_indices
	broken[0] = bad_surface
	if data.set_data_checked(broken) != ERR_INVALID_DATA:
		_failures.append("out-of-range source vertex index was accepted")
	if data.get_surface_count() != 1:
		_failures.append("failed validation destroyed valid data")
	_test_attachment(mesh, data, arrays)
	print("CLUSTER_TEST_INFO: source_indices=%d groups=%d clusters=%d coarse_indices=%d" % [source_indices.size(), groups.size() / 5, clusters.size() / 3, previous_triangle_count])
	_finish()


func _test_attachment(mesh: ArrayMesh, data: MeshClusterData, arrays: Array) -> void:
	var source_rid := mesh.get_rid()
	if not data.matches_mesh(mesh):
		_failures.append("generated hierarchy does not match source")
	mesh.cluster_data = data
	if mesh.cluster_data != data or mesh.get_rid() != source_rid:
		_failures.append("cluster attachment replaced source mesh identity")
	var material := StandardMaterial3D.new()
	mesh.surface_set_material(0, material)
	mesh.surface_set_name(0, "terrain")
	if mesh.cluster_data != data:
		_failures.append("material or name edit discarded geometry cache")
	var path := "user://cluster_mesh_test.tres"
	var saved := ResourceSaver.save(mesh, path)
	var loaded := ResourceLoader.load(path, "ArrayMesh", ResourceLoader.CACHE_MODE_IGNORE) as ArrayMesh
	if saved != OK or loaded == null or loaded.cluster_data == null:
		_failures.append("attached ArrayMesh resource round trip failed")
	elif not loaded.cluster_data.matches_mesh(loaded) or loaded.surface_get_name(0) != "terrain":
		_failures.append("loaded hierarchy/source or surface metadata mismatch")
	var other_arrays := arrays.duplicate(true)
	var positions: PackedVector3Array = other_arrays[Mesh.ARRAY_VERTEX]
	positions[0] += Vector3(0, 2, 0)
	other_arrays[Mesh.ARRAY_VERTEX] = positions
	var other := ArrayMesh.new()
	other.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, other_arrays)
	if data.matches_mesh(other):
		_failures.append("signature accepted different geometry with equal counts")
	# A shared cache changing to another source must detach all old owners.
	var peer := ArrayMesh.new()
	peer.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	peer.cluster_data = data
	if data.generate(other) != OK or mesh.cluster_data != null or peer.cluster_data != null:
		_failures.append("changed shared hierarchy left stale source attachments")
	if mesh.generate_cluster_lods() != OK or mesh.cluster_data == null:
		_failures.append("ArrayMesh hierarchy convenience generation failed")
	var retained := mesh.cluster_data
	if mesh.generate_cluster_lods(1) != ERR_INVALID_PARAMETER or mesh.cluster_data != retained:
		_failures.append("failed generation destroyed attached cache")
	# Even an identity update invalidates derived geometry before submission.
	var vertex_bytes: PackedByteArray = mesh.get("_surfaces")[0].vertex_data
	mesh.surface_update_vertex_region(0, 0, vertex_bytes)
	if mesh.cluster_data != null:
		_failures.append("vertex region update retained derived cache")
	if mesh.generate_cluster_lods() != OK:
		_failures.append("hierarchy recook after region update failed")
	var attribute_bytes: PackedByteArray = mesh.get("_surfaces")[0].attribute_data
	mesh.surface_update_attribute_region(0, 0, attribute_bytes)
	if mesh.cluster_data != null:
		_failures.append("attribute region update retained derived cache")
	mesh.cluster_data = retained
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	if mesh.cluster_data != null:
		_failures.append("added surface retained obsolete hierarchy")
	if mesh.generate_cluster_lods() != OK:
		_failures.append("multi-surface hierarchy generation failed")
	mesh.surface_remove(1)
	if mesh.cluster_data != null:
		_failures.append("removed surface retained obsolete hierarchy")
	mesh.cluster_data = retained
	mesh.clear_surfaces()
	if mesh.cluster_data != null:
		_failures.append("cleared mesh retained hierarchy")


func _finish() -> void:
	if _failures.is_empty():
		print("CLUSTER_TEST_PASS: native cook, complete cuts, boundary locks, source preservation, checked cache, ArrayMesh attachment, signatures, shared mutation, round trip and invalidation")
	else:
		for failure in _failures:
			printerr("CLUSTER_TEST_FAIL: ", failure)
	get_tree().quit(0 if _failures.is_empty() else 1)
