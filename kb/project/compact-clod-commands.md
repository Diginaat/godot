---
type: Decision
title: Compact CLOD command streams drawn with a GPU count
description: CLOD selection and HZB kernels append only the groups they draw and count them; passes draw with draw_list_draw_indirect_count. hzb_bench GPU frame 2.2-5.5x faster on Vulkan, 1.6-3.3x on D3D12; two-pass occlusion no longer a loss.
tags: [geometry, clod, occlusion, hzb, indirect, rendering, renderingdevice]
status: stable
sources:
  - id: vk
    title: Vulkan specification, vkCmdDrawIndexedIndirectCount (VK_KHR_draw_indirect_count, core in Vulkan 1.2, feature drawIndirectCount)
    url: https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdDrawIndexedIndirectCount.html
  - id: d3d12
    title: Direct3D 12, ID3D12GraphicsCommandList::ExecuteIndirect (pCountBuffer)
    url: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect
  - id: gpudriven
    title: Haar and Aaltonen, GPU-Driven Rendering Pipelines, SIGGRAPH 2015 Advances in Real-Time Rendering
  - id: bench
    title: devtools/test/hzb_bench.ps1 -Commands ab, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Problem

Every CLOD pass consumed one 20-byte indirect record per cluster group of
every selected surface, with zero instances for groups outside the cut
([finding](/project/clod-two-pass-zero-draws.md)): about 227 k records
per pass in `hzb_bench`, nearly all empty. The command processor reads
each one. Two-pass occlusion read them three times and was 27-32% slower
than no occlusion.

# Decision

- RenderingDevice gets `draw_list_draw_indirect_count()` (graph
  instruction `TYPE_DRAW_INDIRECT_COUNT`, count buffer tracked as an
  indirect read) and `SUPPORTS_DRAW_INDIRECT_COUNT`: Vulkan enables the
  1.2 feature `drawIndirectCount` when the device has it[^vk], D3D12 uses
  `ExecuteIndirect`'s count buffer[^d3d12], Metal reports it unsupported
  (its driver code is a stub). C++ only, like `SUPPORTS_MULTI_DRAW_INDIRECT`.
- The selection writes one **selection word** per group (first index, or
  `UINT32_MAX`), and appends drawn groups to the selected stream with an
  atomic per-surface counter. The HZB split and occlude kernels read the
  selection words (not the commands) and append to the phase-1, colour
  and phase-2 streams. Counts: one buffer, four regions of
  `draw_capacity` words, cleared once per frame. This is the
  compaction step of GPU-driven pipelines[^gpudriven].
- The colour pass now draws its own stream (`color_commands`) instead of
  the selection's commands rewritten in place.
- Fallback: with `compact` off every kernel writes one record per group
  at the group's slot, exactly the old layout. Used when the device lacks
  indirect count or multi-draw, in the Cluster LOD debug view (it pushes
  each group id per record), and with
  `rendering/geometry/cluster_lod/compact_commands = false` (default on;
  for comparison and driver workarounds).
- Invariant: a surface's command offset is a multiple of 5 words, so its
  selection words start at offset / 5.

Considered: keeping per-group records and adding only a phase-2 count
(saves one stream, leaves the empty reads); a prefix-sum scatter for
deterministic order (one more dispatch; order inside a surface does not
change an opaque depth-tested image, checked below).

# Measured

`hzb_bench` (street grid, 225 CLOD objects, 14.9 M source triangles,
1920x1080, RTX 5070, both settings alternating in one process, 2
repeats x 240 frames, medians)[^bench]. GPU frame p50 in ms, one record
per group -> compact:

| View | off | single | two_pass | two_pass+mm2 |
| --- | --- | --- | --- | --- |
| Vulkan street | 1.917 -> 0.815 | 1.997 -> 0.846 | 2.621 -> 0.815 | 2.204 -> 0.401 |
| Vulkan corner | 1.341 -> 0.626 | 1.403 -> 0.642 | 1.811 -> 0.624 | 1.546 -> 0.369 |
| Vulkan aerial | 2.234 -> 0.917 | 2.345 -> 1.003 | 3.093 -> 1.001 | 2.768 -> 0.697 |
| D3D12 street | 1.759 -> 1.031 | 1.784 -> 0.996 | 2.251 -> 0.960 | 1.853 -> 0.566 |
| D3D12 corner | 1.304 -> 0.836 | 1.321 -> 0.815 | 1.629 -> 0.795 | 1.388 -> 0.559 |
| D3D12 aerial | 1.960 -> 1.064 | 1.982 -> 1.067 | 2.533 -> 1.073 | 2.234 -> 0.772 |

Depth pre-pass (Vulkan street two_pass) 1.599 -> 0.389 ms, opaque 0.897
-> 0.301 ms. With compact streams two_pass equals off on street and
corner (Vulkan 0.815 / 0.815, 0.624 / 0.626) and is 9% slower in the
aerial view, where almost nothing is occluded. p95 follows p50.

# Tests

- `clod.gd`: compact rounds check the counts, the set of drawn groups and
  every selection word against the CPU oracle, plus guards.
- `hzb.gd`: 4 rounds with one record per group and 4 compact rounds:
  colour, phase-1 and phase-2 streams, counts and history.
- `hzb_two_pass.gd` section E: one record per group draws images equal to
  the compact references in off, single and two-pass modes.
- Large-world suite (22) passes on Vulkan and D3D12; standard smoke passes.

# Limits and next

- Not run on GTX 10/16-series, AMD or Intel here; the path is chosen by
  the runtime feature, and the record fallback is the old code.
- The GPU instance path (D038) and the MultiMesh cull still issue one
  indirect draw per bucket level, empty levels included: the next user
  of `draw_list_draw_indirect_count`.
- The CLOD statistics' `DRAW_CALLS_IN_FRAME` still counts group records.
