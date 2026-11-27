---
type: Finding
title: The HZB build was bound by barriers between its dispatches
description: The occlusion pyramid was built with one compute dispatch per level, with a barrier after each (11 levels at 1152 x 648). In a 1,000-node scene that was 0.018 ms of a 0.18 ms GPU frame, nearly half of the gap to stock. Reducing four levels per dispatch in shared memory cut it to 0.010 ms, and 0.033 to 0.021 ms on forest_map, with every level bit-identical.
tags: [hzb, occlusion, compute, gpu, small-scenes]
status: stable
sources:
  - id: intervals
    title: misc/large_world_test_project/gpu_intervals.gd on instance_bench.gd --case=small_1k and forest_map.tscn, Vulkan, RTX 5070, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

GPU time of `small_1k` (1,000 MeshInstance3D nodes, static view, with
`--gpu-profile`) per timestamp interval, stock 4.7.2 against the fork
with its default GPU paths:

| Interval | Stock (ms) | Fork (ms) |
| --- | --- | --- |
| Depth pre-pass | 0.010 | 0.010 (plus GPU cull 0.005) |
| HZB build | - | 0.018 |
| HZB occlusion and phase 2 | - | 0.006 |
| Shadows | 0.035 | 0.034 (shadow cull included) |
| Opaque | 0.077 | 0.080 |
| Whole frame | 0.142 | 0.181 |

The pyramid (half the depth's resolution, padded to powers of two:
1024 x 512 down to 1 x 1) had 11 levels, built by 11 dispatches with a barrier
after each. Most of the later levels are a few texels, so their time is
the barrier and the dispatch, not the work.

# Change

`mesh_hiz_reduce.glsl` takes `LEVELS` (1 to 4): each 8 x 8 group writes
its level-0 texels, then reduces them in shared memory to the 4 x 4,
2 x 2 and 1 x 1 texels of the next levels. Levels are powers of two
(padded pyramid), so a tile is either whole or the only one starting at
0, and the per-level clamp at a 1-texel side becomes a clamp to the
level's size inside the tile. `_build_hzb()` dispatches every four levels
(11 levels: 3 dispatches), with eight pipelines (first level or not,
times 1-4 levels).

# Results

| Scene | HZB build before (ms) | After (ms) | Whole frame before / after (ms) |
| --- | --- | --- | --- |
| small_1k | 0.018 | 0.010 | 0.181 / 0.173 |
| forest_map | 0.033 | 0.021 | 1.490 / 1.462 |

`hzb.gd` checks every level against the per-level CPU reduction
exactly: the 13 x 9 depth of the occlusion test in one dispatch, and a
new 75 x 9 depth (64 x 8 pyramid, 7 levels, 8 groups, sides that reach 1
texel first) in two. Passes on Vulkan and D3D12.

# Open

The rest of the small-scene gap is the fixed cost of the GPU-driven
paths: culling dispatches (0.009 ms), the occlusion test, and the HZB
itself. Skipping them in small scenes would need a heuristic for when
occlusion pays off; not done.
