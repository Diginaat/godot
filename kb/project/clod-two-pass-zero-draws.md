---
type: Finding
title: CLOD two-pass occlusion costs more than it saves (zero-instance draws)
description: Every CLOD pass issues one indirect record per group, selected or not; two-pass adds a third full set, about 1.1 ms in hzb_bench, so two_pass is 27-32% slower than no occlusion.
tags: [geometry, clod, occlusion, hzb, indirect, rendering]
status: resolved
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What

`hzb_bench.tscn` with every dense object on cluster LOD (2026-11-24, RTX
3060, Vulkan), GPU p50 / depth p50:

| View | off | single | two_pass |
| --- | --- | --- | --- |
| street | 3.699 / 1.656 | 3.801 / 1.733 | 4.821 / 2.765 |
| corner | 2.662 / 1.145 | 2.696 / 1.183 | 3.379 / 1.871 |
| aerial | 4.302 / 1.953 | 4.370 / 2.021 | 5.695 / 3.252 |

# Why

The selection writes one 5-word command per cluster group of every
selected surface, with zero instances for groups outside the cut, and each
pass consumes the whole range with one multi-draw
(`draw_list_draw_indirect(..., group_count, ...)`). 191 surfaces of about
1,190 groups give about 227 k records per pass, nearly all empty. Two-pass
reads them three times (phase-1 depth, phase-2 depth, colour) instead of
twice, and the extra depth pass costs about 1.1 ms; the groups it saves are
cheap (the cut is already coarse).

# Fix idea

Compact: write the selected groups' commands densely with a count, and draw
with indirect count (`vkCmdDrawIndexedIndirectCount`,
`ExecuteIndirect` with a count buffer). Both Vulkan and D3D12 drivers
implement `command_render_draw_indexed_indirect_count`, but RenderingDevice
has no `draw_list_draw_indirect_count` (it needs a draw-graph command). This
would also cut the one-pass and colour cost. Until then the CLOD two-pass
default should be reviewed (`rendering/geometry/cluster_lod/occlusion_two_pass`).

# Resolved

2026-11-27: [compact CLOD command streams](/project/compact-clod-commands.md)
draw only the selected groups with `draw_list_draw_indirect_count()`; two_pass
now equals off on street and corner and is 9% slower in the aerial view.
