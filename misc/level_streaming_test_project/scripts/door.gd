extends AnimatableBody3D

# A door whose open state survives the cell being unloaded: the node is in
# the "level_stream_persistent" group, and LevelStreamer3D calls the two
# methods below when it unloads and reloads the cell.

@export var open := false:
	set(value):
		open = value
		if is_node_ready():
			_apply()


func _ready() -> void:
	_apply()


func _apply() -> void:
	# The hinge is at the node's origin; open swings the door into the shop.
	rotation.y = PI * 0.5 if open else 0.0


func _save_stream_state() -> Variant:
	return {"open": open}


func _load_stream_state(state: Variant) -> void:
	open = state.get("open", false)
