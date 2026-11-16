# Scene runtime: session memory

Entry point for the hybrid data-oriented SceneTree work. Read this first,
then the [roadmap](../implementation/scene-runtime-roadmap.md). Keep it
short; details go in the linked files.

## State

| Item | Value |
| --- | --- |
| Branch | `feat/scene-runtime` (from `feat/core-stl-simd-modernization` at `12cd0e0924`), worktree `GodotPhysxDev-core` |
| Phase | 1 (hierarchy and groups), 2026-11-16 |
| Completed | M0: source analysis, three approaches compared, A chosen ([architecture](../architecture/hybrid-scene-runtime.md)), scenario benchmark and baseline ([numbers](../benchmarks/scene-runtime.md)); M1: `add_to_group` without the linear duplicate check (`instance` 100k -60%, `churn` 100k -51%); M2: children cache holes compacted on read (`churn_read` -15-17%) |
| Next | M3 (`remove_from_group`: a hole at the member's position recorded in `Node::GroupData`, compaction in `_update_group_order()`; every group reader already calls it first), then M4 |
| Builds | Editor `production=yes tests=yes debug_symbols=yes`; release `target=template_release production=yes debug_symbols=yes disable_path_overrides=no` (for `--path`) |

## Design decisions

- S001: approach A first (targeted fixes inside the existing structures, no
  duplicate state); B (contiguous transform store) only as a measured
  experiment for deep / heavy-update scenes; C (ECS beneath the nodes)
  rejected for its semantic and maintenance cost.
- S002: compare candidates against a control build of the same source when
  effects are a few percent (code layout moves a build by about 1.5%).

## Benchmark findings

- Profiles (core branch): children cache rebuilds 5.8% and group add
  scans 2.0% of a node-churn main thread; allocations 14.6% of scene
  instancing (signals, renderer tracking, ClassDB creation).
- Baseline: `instance` and `churn` scale quadratically (group scans);
  `deep`, `wide`, `parents`, `heavy` turn memory-bound at 100k nodes (a
  Node3D is about 1.45 KB). Release is 10-25% faster than editor builds
  where allocations or scripts dominate.
- The benchmark project needs `low_processor_mode_sleep_usec=0` (headless
  sleeps 6.9 ms per frame otherwise); release templates need
  `disable_path_overrides=no` and an editor-made `.godot` cache.

- New per-node fields go into existing padding: M2's first layout made
  each node 8 bytes larger and `wide` at 100k 9% slower.

## Known issues

- Editor builds measure with `DEBUG_ENABLED` allocation accounting; use the
  release template for final numbers.

## Failed experiments

- None yet on this branch (core branch: one-block HashMap, packed Vector3 SSE).

## Resume

1. `git status -sb`, `git log -3 --oneline` in `GodotPhysxDev-core`.
2. Read the roadmap row of the next milestone and the benchmark page.
3. Build, run `devtools/test/scene_bench.ps1` for the touched scenarios
   with the previous binary copied as a baseline.
