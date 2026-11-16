# Scene runtime benchmarks

How: `devtools/test/scene_bench.ps1` runs `devtools/test/scene_bench.gd` in
`misc/scene_bench_project`, one process per scenario and node count,
headless (no GPU work; `-Visual` for MeshInstance3D with a window), builds
alternated per round with the order reversed every other round. Each run:
30 warm-up frames and 120 measured ones (5 and 30 at 100k nodes). It
reports the wall time per frame (p50, p95), the time of the scenario's own
operations in the frame (`op`: the setter calls with their eager
propagation, reads of global transforms, add / free / reparent /
instantiate), setup time and static memory (editor builds only; release
builds do not count allocations). The benchmark project sets
`application/run/low_processor_mode_sleep_usec=0`: without a window that can
draw, the main loop otherwise sleeps 6.9 ms per frame.

Machine: Ryzen 5 3600 (6 cores, 12 threads), Windows 11, MSVC.
Builds: editor `production=yes tests=yes debug_symbols=yes`; release
`target=template_release production=yes debug_symbols=yes
disable_path_overrides=no`.

| Scenario | Each frame |
| --- | --- |
| `idle` | nothing (balanced tree, branching 8) |
| `deep` | chains of 100 nodes: move each chain's root, read each leaf's global position |
| `wide` | one parent with N children: move it, read every 100th child |
| `parents` | N / 100 parents with 100 children each: rotate every parent |
| `sparse` | move 1% of a balanced tree's nodes |
| `heavy` | move every node of a balanced tree |
| `script` | N nodes with a GDScript `_process` setting their position |
| `native` | N running Timers (internal processing) |
| `churn` | free and add 2% of N leaf nodes under 10 parents, each in one group |
| `churn_read` | as `churn`, then one child read per parent (as `get_index()` and `call_group()`'s tree-order sort do; added with M2) |
| `reparent` | reparent 1% of N nodes between two parents |
| `instance` | instantiate N / 100 copies of a 100-node scene (nodes in a group), free the previous ones |
| `mixed` | move 0.5% of N/2 Node3D and N/2 Node2D nodes |

## Baseline, 2026-11-16

Branch `feat/scene-runtime` at `12cd0e0924` (upstream scene code), 3
rounds, ms per frame, median (range):

| Scenario | 1k editor | 1k release | 10k editor | 10k release | 100k editor | 100k release |
| --- | --- | --- | --- | --- | --- | --- |
| idle | 0.002 | 0.002 | 0.002 | 0.002 | 0.002 | 0.002 |
| deep | 0.049 | 0.048 | 0.533 | 0.519 | 34.6 (34.6-34.6) | 33.6 (33.5-33.7) |
| wide | 0.009 | 0.009 | 0.074 | 0.073 | 3.97 (3.93-4.03) | 3.73 (3.63-4.26) |
| parents | 0.008 | 0.008 | 0.070 | 0.070 | 3.92 (3.88-3.95) | 3.53 (3.53-3.58) |
| sparse | 0.006 | 0.004 | 0.037 | 0.027 | 0.775 (0.757-0.838) | 0.574 (0.569-0.582) |
| heavy | 0.177 | 0.143 | 1.88 | 1.52 | 56.7 (55.5-56.9) | 49.7 (49.2-51.3) |
| script | 0.355 | 0.291 | 4.96 (3.63-5.08) | 3.12 (2.87-3.79) | 56.9 (56.3-58.5) | 46.8 (45.2-48.0) |
| native | 0.027 | 0.031 | 0.245 | 0.294 | 11.9 (11.7-11.9) | 11.5 (11.2-11.7) |
| churn | 0.070 | 0.057 | 1.57 | 1.43 | 98.7 (96.8-99.3) | 97.1 (94.8-97.2) |
| reparent | 0.018 | 0.014 | 0.153 | 0.120 | 2.21 (2.20-2.26) | 1.82 (1.80-1.83) |
| instance | 1.77 | 1.55 | 41.8 (41.7-42.1) | 39.1 (38.8-39.4) | 2,041 (2,037-2,049) | 2,000 (1,997-2,030) |
| mixed | 0.007 | 0.005 | 0.045 | 0.035 | 0.799 | 0.613 |

Almost all of each frame is the scenario's own operations (`op` within 1-5%
of the frame), except `script` and `native`, whose cost is the engine's
processing of the nodes.

Memory (editor build, static): a `Node3D` about 1.45 KB, a `Timer` (a
`Node`) about 1.05 KB; 100k Node3D nodes about 185 MB. Setup of 100k nodes
110-215 ms.

What the scaling from 10k to 100k nodes (10x) shows:

- `instance` x49 and `churn` x63: quadratic. Every node of an instance is in
  the group "parts" (every churned node in "churned"); each one entering
  the tree scans the whole group (`SceneTree::add_to_group`'s duplicate
  check, M1) and each one freed searches and shifts it
  (`remove_from_group`, M3).
- `deep` x65, `wide` x54, `parents` x56, `heavy` x30, `sparse` x21: linear
  work that turns memory-bound once 100k nodes (about 145 MB) no longer fit
  in the caches; every node visited in propagation or recomputation is a
  miss.
- Release is 10-25% faster than the editor build where allocations or
  scripts dominate (`script`, `heavy`, `sparse`, `reparent`), the same
  elsewhere; editor A/Bs are representative for relative changes, release
  numbers are reported for kept changes.

## Results per milestone

### M1: `SceneTree::add_to_group` without the linear duplicate check, 2026-11-16

Release builds, base and M1 alternated, 3 rounds, ms per frame, median
(range):

| Scenario | 1k | 10k | 100k |
| --- | --- | --- | --- |
| churn | 0.057 to 0.051 | 1.40 (1.37-1.40) to 0.881 (0.872-0.885) | 95.5 (94.7-95.7) to 46.4 (46.3-46.5) |
| instance | 1.54 to 1.38 | 38.9 (38.8-39.6) to 26.3 (26.3-26.9) | 2,002 (1,999-2,020) to 798 (798-799) |
| sparse (no groups, control) | 0.005 / 0.004 | 0.027 / 0.027 | 0.569 / 0.564 |

Every round lower, ranges apart; the control is unchanged. The rest of the
quadratic cost of `churn` is `remove_from_group`'s search and shift (M3).

### M2: holes in the children cache instead of a rebuild, 2026-11-16

Release builds, M1 and M2 alternated, 3 rounds, ms per frame, median
(range):

| Scenario | 1k | 10k | 100k |
| --- | --- | --- | --- |
| churn_read | 0.064 to 0.055 | 1.106 (1.092-1.117) to 0.945 (0.933-0.955) | 58.2 (58.0-58.4) to 48.6 (48.5-48.9) |
| churn (no reads) | 0.051 / 0.051 | 0.885 (0.882-0.891) / 0.903 (0.892-0.913) | 46.6 / 46.6 |
| wide | 0.008 / 0.008 | 0.074 / 0.073 | 3.77 (3.67-3.81) / 3.73 (3.69-3.76) |

The profiled workload of the core branch (10k `MeshInstance3D` under one
parent, 200 freed and added per frame, a `call_group()` every frame;
editor builds, 5 rounds): 13.88 (13.81-13.95) to 13.00 (12.96-13.29) ms.
A first version put the new field after the cache vector, which made each
node larger: `wide` at 100k was 9% slower; next to the `bool` (in its
padding) it is not. `churn` at 10k is +2%, within the 1.5% that code layout
alone moves a build and not seen at 1k or 100k (not checked with a control
build).
