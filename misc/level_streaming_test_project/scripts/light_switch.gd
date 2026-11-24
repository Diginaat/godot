extends OmniLight3D

# A light switch that keeps its state while the cell is unloaded.

@export var on := true:
	set(value):
		on = value
		visible = value


func _save_stream_state() -> Variant:
	return {"on": on}


func _load_stream_state(state: Variant) -> void:
	on = state.get("on", true)
