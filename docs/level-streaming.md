# Level streaming

`modules/level_streaming` adds two nodes for 3D level streaming. They are
opt-in: a project without a `LevelStreamer3D` does no extra work per frame.

- `LevelStreamer3D` streams the cells below it around a target node.
- `LevelStreamCell3D` is one streamed part of a level. It holds an ordinary
  `.tscn` file, a box, preload and unload distances, a stable ID and its
  neighbors.

The class reference (`LevelStreamer3D`, `LevelStreamCell3D` in the editor
help) describes every property, method and signal.

## Minimal setup

1. Make each part of the level an ordinary scene (a street block, a shop
   interior). Author it centered on its origin. The streamer adds it as a
   child of its cell.
2. In the level scene, add a `LevelStreamer3D`. Set **Target Path** to the
   player or vehicle. If it is empty, the current camera is the target.
3. Add a `LevelStreamCell3D` below the streamer for each part. Set:
   - **Scene Path**: the part's `.tscn`.
   - **Size**: the part's box. The gizmo shows the box, and the preload
     and unload distances around it.
   - **Preload Distance** and **Unload Distance**. The cell loads when the
     target comes within the preload distance of its box, and unloads
     beyond the unload distance. Keep the unload distance larger.
   - **Neighbors**: cells that load whenever the target is inside this
     cell, at any distance. Use this for the interior behind a door.
   - **Cell ID**: set from the node name when the cell is created. Keep it
     when you rename the node: saved state is keyed by it.
4. Run the game. The editor shows every cell's scene for layout. The preview
   is never saved into the level.

The editor warns about these problems:

- a cell outside a streamer
- a missing or duplicate ID
- a missing scene
- an unload distance smaller than the preload distance
- an empty box
- an unknown neighbor
- a missing target

## Exporting

The cells' scenes export with the project, also when the preset exports
only selected scenes and their dependencies. A level names its cells'
scenes only by path, so they are not dependencies that Godot finds on its
own. The module's export plugin adds them through
`EditorExportPlugin::get_runtime_dependencies()`. This is a fork hook,
called from `_export_find_dependencies()`. The scenes still load only when
a cell needs them.

## State that survives unloading

A node in a cell's scene keeps its state across an unload and reload if it
does two things:

- It is in the group `level_stream_persistent`.
- It defines `_save_stream_state()` and `_load_stream_state(state)`.

```gdscript
extends AnimatableBody3D

@export var open := false

func _save_stream_state() -> Variant:
	return {"open": open}

func _load_stream_state(state: Variant) -> void:
	open = state.get("open", false)
```

`_load_stream_state()` runs before the node enters the tree, so `_ready()`
sees the restored state. The state is keyed by cell ID and node path. For
save games, read and write it with `get_cell_state_data()` and
`set_cell_state_data()`.

## Readiness and teleports

A cell becomes **active** when its content enters the tree. It becomes
**ready** `ready_physics_frames` physics frames later (default 2). By then
its bodies are in the physics space and its navigation regions are in the
map. `is_position_ready(point)` tells whether the target can be at a point.

To teleport:

```gdscript
streamer.prefetch_position(destination)
await streamer.destination_ready
player.global_position = destination
streamer.clear_prefetch()
```

`prefetch_position()` loads the destination's cells and their neighbors at
any distance, and keeps them loaded until `clear_prefetch()`. If a
destination scene fails to load, `cell_failed` is emitted and
`destination_ready` is not. The cell is tried again after `retry_delay`.

## How it works

`LevelStreamer3D` runs `_update()` from internal process while it is
active and has cells. Each update does these steps:

1. **Wanted set.** Each cell's distance to the target is measured in the
   cell's own space, so rotated cells work. A loaded cell stays wanted up
   to its unload distance; an unloaded one becomes wanted at its preload
   distance. The neighbors of the cell that contains the target are
   wanted, and so are the cells pinned by `prefetch_position()`.
2. **Poll.** Finished threaded loads are collected with
   `ResourceLoader::load_threaded_get()`. Every request is collected, also
   that of a removed cell, because a request can't be cancelled. A result
   that is no longer wanted, or whose `scene_path` changed, is dropped
   (counted as `stale`). Otherwise the scene is instantiated on a
   `WorkerThreadPool` task (`threaded_instantiation`).
3. **Start.** Wanted cells start loading, nearest first, up to
   `max_loading` at a time. A request for a cell that is already loading
   is not repeated.
4. **Activate and unload.** At most `max_activations_per_frame` cells are
   added to the tree, nearest first. At most as many are removed. An
   instance whose cell is no longer wanted is freed without entering the
   tree.
5. **Readiness.** Active cells that have waited their physics frames
   become ready. `destination_ready` is emitted when every pinned cell is
   ready.

## Acceptance test

`misc/level_streaming_test_project` is a clean project that uses only the
nodes above. It has these cells:

- ten street cells, each with its own sign texture, neon and spot lights,
  collision and a navigation mesh
- a shop interior behind a door, with a light switch
- a plaza with an annex as its neighbor
- a depot

`generate.py` writes the scenes. The export preset exports only
`main.tscn`.

`test_driver.gd` does these steps in order:

1. Drives down the street.
2. Enters the shop, opens its door and switches its light off.
3. Turns straight back, then drives 120 m away so the shop unloads.
4. Comes back. It checks the door and light state, the sign texture, the
   light, the floor collision, a ray through the doorway, and a navigation
   path from the street into the shop. It also compares a frame of a
   reloaded street cell with the same frame before the unload.
5. Crosses a preload and an unload boundary 12 times.
6. Teleports back and forth faster than cells load, and checks that the
   cells end up consistent.
7. Does a cold teleport with `prefetch_position()`.
8. Teleports to a destination whose scene is missing. It checks that the
   game keeps running and recovers once the scene is fixed.

On every physics frame of the drive, it checks that the player's cell is
ready and that there is ground under the player.

Run it on Linux (the editor and `template_release` built, and the template
installed with the PhysX and Blast libraries next to it):

```
devtools/test/level_streaming.sh bin/level_streaming_results 3
```

The script imports and exports the project and copies the export out. It
makes the source project unreadable while the game runs. Then it alternates
streamed and fully preloaded (`--preload-all`) runs. Each run writes JSON
with these measurements:

- frame times (p50, p95, p99, max), overall and per phase
- boundary frames: the frame of an activation or unload and the 2 after it
- each activation's main thread cost
- readiness delays
- peak RSS and video memory

Results: see [the KB entry](../kb/project/level-streaming.md).
