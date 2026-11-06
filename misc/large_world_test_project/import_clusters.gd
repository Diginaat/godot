extends Node
## Reads actual editor-imported scene caches; no script-side hierarchy cook.


func _ready() -> void:
	var failures: Array[String] = []
	for fixture in ["enabled", "disabled", "morph"]:
		var scene := load("res://import_fixtures/%s.gltf" % fixture) as PackedScene
		if scene == null:
			failures.append("scene import failed: " + fixture)
			continue
		var root := scene.instantiate()
		var meshes := root.find_children("*", "MeshInstance3D", true, false)
		if meshes.is_empty():
			failures.append("imported scene has no native mesh instances: " + fixture)
			root.free()
			continue
		var first: ArrayMesh = meshes[0].mesh
		if first == null or first.get_surface_count() != 1:
			failures.append("import changed source mesh surfaces: " + fixture)
		elif fixture == "enabled":
			if first.cluster_data == null or not first.cluster_data.matches_mesh(first):
				failures.append("enabled import did not serialize matching cluster data")
			if first.shadow_mesh == null:
				failures.append("cluster cook removed conventional optimized shadow mesh")
			if meshes.size() != 2 or meshes[1].mesh != first:
				failures.append("cluster cook broke shared imported mesh identity")
		elif first.cluster_data != null:
			failures.append("disabled/unsupported import attached cluster hierarchy: " + fixture)
		if fixture == "morph" and first.get_blend_shape_count() != 1:
			failures.append("unsupported cluster cook changed imported morph geometry")
		root.free()
	if failures.is_empty():
		print("IMPORT_CLUSTER_TEST_PASS: real scene import enabled/default-off, serialized signatures, shared meshes, conventional shadow mesh and unsupported morph fallback")
	else:
		for failure in failures:
			printerr("IMPORT_CLUSTER_TEST_FAIL: ", failure)
	get_tree().quit(0 if failures.is_empty() else 1)
