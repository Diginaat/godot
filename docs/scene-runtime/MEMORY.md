# Scene runtime: session memory

Entry point for the hybrid data-oriented SceneTree work. Read this first,
then the [roadmap](../implementation/scene-runtime-roadmap.md). Keep it
short; details go in the linked files.

## State

| Item | Value |
| --- | --- |
| Branch | `feat/scene-runtime` (from `feat/core-stl-simd-modernization` at `12cd0e0924`), worktree `GodotPhysxDev-core` |
| Phase | M6 or M5 rest, 2026-11-16 |
| Completed | M0: source analysis, three approaches compared, A chosen ([architecture](../architecture/hybrid-scene-runtime.md)), scenario benchmark and baseline ([numbers](../benchmarks/scene-runtime.md)); M1: `add_to_group` without the linear duplicate check (`instance` 100k -60%, `churn` 100k -51%); M2: children cache holes compacted on read (`churn_read` -15-17%); M3: group removals by recorded position (`churn` 100k -81%, `instance` 100k -70%); M4: Node3D propagation stops at dirty subtrees walked in the current notification epoch (`heavy` op 100k -32%); M5: node groups in a vector, `SNAME` in instancing (`instance` -20-24%, `churn` -8-10%, node -24 bytes) |
| Next | M6 (process lists, only if a script-free profile shows them), or the rest of M5 (children map and owned list element allocations, signals) if a profile ranks them higher |
| Builds | Editor `production=yes tests=yes debug_symbols=yes`; release `target=template_release production=yes debug_symbols=yes disable_path_overrides=no` (for `--path`) |

## Design decisions

- S001: approach A first (targeted fixes inside the existing structures, no
  duplicate state); B (contiguous transform store) only as a measured
  experiment for deep / heavy-update scenes; C (ECS beneath the nodes)
  rejected for its semantic and maintenance cost.
- S002: compare candidates against a control build of the same source when
  effects are a few percent (code layout moves a build by about 1.5%).
- S003: judge scene-side changes on `op` time and headless runs
  (`--headless --visual` for MeshInstance3D notifications); windowed
  `-Visual` frames are renderer-bound and move 2-11% with code layout.
- S004 (M4): no work per transform notification. The early-out's
  invariant is kept by an epoch (`SceneTree::xform_change_epoch`, bumped
  per non-empty flush and when a dirty node starts to want notifications),
  not by cleaning notified nodes.

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

- Group scans were the whole quadratic cost: after M1 and M3, `churn` at
  100k is 97.1 to 9.05 ms, `instance` 2,000 to 247 ms.

## Known issues

- Editor builds measure with `DEBUG_ENABLED` allocation accounting; use the
  release template for final numbers.
- The worktree's `bin/` needs the Streamline runtime (`sl.*`, `nvngx_*`,
  copied from the main checkout) or the PhysX smoke checks fail on the
  DLSS init error.
- `scene_bench.ps1` list parameters need `& script -Scenarios @(...)`;
  `powershell -File` passes a comma list as one string.

## Failed experiments

- M4 by cleaning notified nodes (in the handler, or after the handlers in
  the flush): 50-75 ns per notified node.

## Resume

1. `git status -sb`, `git log -3 --oneline` in `GodotPhysxDev-core`.
   Read `kb/mistakes/iterated-builds-instead-of-design.md` and
   `kb/mistakes/renderer-bound-frame-for-scene-changes.md`.
2. Read the roadmap row of the next milestone and the benchmark page.
3. Build, run `devtools/test/scene_bench.ps1` for the touched scenarios
   with the previous binary copied as a baseline.
