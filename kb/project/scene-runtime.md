---
type: Finding
title: Scene runtime optimization (SceneTree, Node, Node3D)
description: Six measured changes inside the existing node structures (groups, children cache, transform propagation epoch, group storage, process-loop prefetch) made scene instancing and node churn 10-11x faster at 100k nodes and moving trees 41% faster, with the public API unchanged.
tags: [scene, performance, nodes, transforms, groups]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-16T23:30:00+01:00 }
sources:
  - id: roadmap
    title: Scene runtime roadmap
    url: /docs/implementation/scene-runtime-roadmap.md
  - id: bench
    title: Scene runtime benchmarks
    url: /docs/benchmarks/scene-runtime.md
---

# What changed

Branch `feat/scene-runtime`. Targeted fixes inside Node, SceneTree and
Node3D, chosen from sampling profiles of a 14-scenario benchmark
(`devtools/test/scene_bench.ps1`); no duplicate state, no API change
(`--dump-extension-api` identical before and after). Details per
milestone in the roadmap and the architecture page
(`docs/architecture/hybrid-scene-runtime.md`, "Final state").

# Totals (release, headless, ms per frame, base `12cd0e0924` to final)

| Scenario | 10k | 100k |
| --- | --- | --- |
| instance (scene instancing, nodes in a group) | 40.3 to 18.0 (2.2x) | 2,013 to 188 (10.7x) |
| churn (2% freed and added, in a group) | 1.48 to 0.53 (2.8x) | 97.0 to 8.5 (11.4x) |
| churn_read (churn plus child reads) | 1.72 to 0.63 (2.7x) | 109.3 to 10.6 (10.3x) |
| heavy (every node of a tree moves) | 1.65 to 1.47 (-11%) | 51.8 to 30.8 (-41%) |
| native (Timers, internal processing) | 0.273 / 0.272 | 11.71 to 6.56 (-44%) |
| parents (rotate parents of 100 children) | 0.071 to 0.038 (-46%) | 4.07 to 1.38 (-66%) |
| wide (move a parent of N children) | 0.075 to 0.044 (-41%) | 4.17 to 0.97 (-77%) |
| script (GDScript `_process`) | 3.82 to 3.70 (-3%) | 46.6 to 44.1 (-5%) |
| heavy2d (every Node2D moves) | 2.90 to 2.26 (-22%, noisy) | 41.8 to 39.6 (-5%) |
| deep, mixed, parents2d | within 3% | within 4% |
| sparse | 0.029 / 0.032 (ranges overlap) | 0.635 to 0.587 (-7%) |
| reparent | 0.133 / 0.135 | 1.953 to 2.016 (+3%) |

# What was learned

- Groups made instancing and churn quadratic (a scan per join, a search
  and shift per leave); that was most of the cost at 100k nodes.
- Large node trees are memory-bound: each node is about 1.1-1.4 KB, so a
  process loop or propagation pays DRAM misses per node; prefetching the
  next nodes in the process list cut 100k Timers by 44%.
- Keeping a transform-notification invariant by cleaning each notified
  node cost 50-75 ns per node; an epoch per flush costs nothing per node.
- Windowed MeshInstance3D frames are renderer-bound and move 2-11% with
  code layout; scene changes are judged on their own operations and
  headless runs (see the mistakes pages).
- 2D already had the early stop in transform propagation, and its global
  transforms are rarely read.
