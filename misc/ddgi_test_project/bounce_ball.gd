extends MeshInstance3D
## Bounces the ball of bleed.tscn: side to side, up and down, and toward and
## away from the wall, as the bleed view of main.gd does.

@export var animate := true

var _time := 0.0


func _process(delta: float) -> void:
	if not animate:
		return
	_time += delta
	position = Vector3(sin(_time * 0.5) * 3.0, 0.6 + absf(sin(_time * 1.6)) * 1.2, 0.8 - 0.4 * (0.5 + 0.5 * sin(_time * 0.23)))
