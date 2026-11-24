---
type: Finding
title: Built-in level streamer (LevelStreamer3D, LevelStreamCell3D)
description: Opt-in C++ 3D level streaming. Cells configured in the Inspector, scenes found by the export, threaded load and instantiate, bounded activation, readiness, persistent state. The exported acceptance test passes 6 of 6 runs, and streamed p99 is the same as fully preloaded.
tags: [streaming, scene, export, resource-loader, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-24T00:00:00Z }
sources:
  - id: setup
    title: docs/level-streaming.md (setup, design, test)
  - id: fixture
    title: misc/level_streaming_test_project and devtools/test/level_streaming.sh
---

# What

`modules/level_streaming` has two nodes:

- `LevelStreamer3D` streams the cells below it around a target node.
- `LevelStreamCell3D` is one streamed part of the level: a `.tscn`, a box,
  preload and unload distances, a stable ID and its neighbors.

The setup, the design and the acceptance test are in
[docs/level-streaming.md](../../docs/level-streaming.md).

What the fork already had, and what was reused:

- `ResourceLoader` threaded requests.
- `WorkerThreadPool`. Instantiating off the main thread is safe: nodes
  outside the tree pass the thread guards, and the physics RID owners are
  thread-safe.
- The texture streaming module, unchanged: it streams mips, not scenes.

There was no scene streamer in the fork.

New in the engine: one export hook,
`EditorExportPlugin::get_runtime_dependencies()`. Without it, "export
selected scenes" leaves out scenes that a level names only by path. With
the hook, an export of only `main.tscn` contains all 14 cell scenes, and a
control scene that no cell names is not exported.

# Acceptance results (2026-11-24)

Conditions:

- Hardware and system: RTX 3060, NVIDIA 620.70.06, Vulkan Forward+,
  1920x1080 window, VSync off, no frame cap, Linux.
- Builds: `production=yes` editor and `template_release` at `b31fae70f0`
  plus this change.
- The exported game ran from a copy, with the source project made
  unreadable (`devtools/test/level_streaming.sh`).
- 3 streamed runs alternated with 3 fully preloaded runs
  (`--preload-all`).
- The fixture is light: frames take about 0.8 ms.

All 6 runs pass every check:

- The player's cell is ready on every physics frame of the drive:
  0 not-ready frames and 0 frames without ground, in 3 of 3 streamed runs.
- After the shop is unloaded and reloaded, its door stays open and its
  light stays off.
- On the revisit, the shop's sign texture, light, floor collision,
  doorway ray and navigation path from the street are all there.
- The image of a reloaded street cell matches the image before its unload,
  with a mean difference of 0.0005-0.0006 (values 0-1).
- Crossing the boundaries 12 times loads `street_07` once and unloads
  `street_02` once. Neither comes back.
- After the teleport thrash, cell states and children are consistent, with
  49-53 stale results dropped.
- On a failed destination, `cell_failed` is emitted and `destination_ready`
  is not. The destination recovers after the retry.

| | Streamed (3 runs) | Preloaded (3 runs) |
| --- | --- | --- |
| Frame p50 / p95 / p99 | 0.776 / 1.144 / 1.347 ±0.10 ms | 0.800 / 1.144 / 1.302 ±0.04 ms |
| Driving phases, p99 | 1.28-1.48 ms | 1.33-1.42 ms |
| Boundary frames after startup, p95 / p99 / max | 4.4 / 7.6 / 8.7 ms | none |
| Activation (`add_child`), p50 / max after startup | 58 µs / 6.5 ms | none |
| First cell ready (startup, cold shaders) | 171-284 ms | 175-284 ms (all cells) |
| Cold teleport (`prefetch_position` to `destination_ready`) | 25-34 ms | 0.7 ms (already loaded) |
| Peak RSS | 724-738 MiB | 690-703 MiB |
| Peak video memory | 200-201 MiB | 216 MiB |

Notes:

- The boundary frames are the frame of an activation or unload and the
  2 frames after it. They cluster in the teleport thrash phase (p99
  9.5 ms), which loads and unloads about 2 cells per frame. While driving,
  the boundary frames stay below 4.6 ms, and the preloaded runs have
  driving maxima of 5.6-6.8 ms as well. Spikes of 12-17 ms that are not
  boundary frames appear in both modes: system noise.
- The worst activation, 6.5 ms for `street_09`, happened only during the
  thrash, while worker threads were instantiating. It is a lead for
  optimization lane D: activation contention.
- Two physics frames (33 ms at 60 Hz) of `ready_physics_frames` make up
  most of the cold teleport delay. The load and instantiation take a few
  milliseconds.
- Streaming uses 25-35 MiB more peak RSS than preloading this small level.
  The thrash keeps scenes in flight on the workers. VRAM is 15 MiB lower.

Physics: GodotPhysics3D, the engine default (the fixture does not select
PhysX).

# Unit tests

`modules/level_streaming/tests/test_level_streaming.h` (4 cases, 51
assertions, run with `--test --test-case="*LevelStreaming*"` on a
`tests=yes` build) covers these:

- registration, including cells deeper than direct children
- a duplicate ID rejected, then promoted when the first cell leaves
- re-registration after an ID change
- every configuration warning
- the distance to rotated and scaled cells
- the saved-state copy semantics

The configuration warnings are C++ only, because
`get_configuration_warnings()` is not bound to scripts.

# Not verified

- Authoring was not done by clicking in the Inspector. `generate.py` writes
  the same properties the Inspector edits.
- .NET builds were not tested, and nor were Windows exports.
