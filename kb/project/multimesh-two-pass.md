---
type: Finding
title: Two-pass MultiMesh occlusion in the depth pre-pass
description: GPU-culled MultiMesh instances hidden last frame are left out of the depth pre-pass; exact images; street grid 7-12% faster, open valley 1-7% slower.
tags: [geometry, occlusion, hzb, multimesh, gpu, rendering]
status: stable
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What

`rendering/geometry/multimesh_gpu_culling/occlusion_two_pass` (default on),
GPU-first plan G3 for the MultiMesh path. Before it, MultiMesh HZB occlusion
removed hidden instances from the colour passes only; the depth pre-pass drew
every frustum-visible instance. CLOD already had two passes.

# How

`multimesh_cull.glsl` modes 5 (`MODE_SPLIT`) and 6 (`MODE_PHASE2`), driven
from `mesh_hiz.cpp`:

- History: one word per instance per MultiMesh transform buffer, two
  buffers ping-ponged (`MultiMeshCullBuffers::history`, `history_write`).
  `history_layout` maps a transform buffer to its (offset, count) in the
  buffer written last frame; offsets go to the shader in
  `OcclusionData.params.zw`. No entry or a different count: no history, so
  phase 1 draws all of that MultiMesh's frustum-visible instances.
- Before the pre-pass, `_split_multimesh_occlusion()` ranks the
  frustum-visible instances that were visible last frame into
  `phase1_ids`/`phase1_commands` (split, scan, scatter). Split keeps bit 30
  on every frustum-visible rank word so the occlusion test still sees them;
  level bits are 24-29.
- After the HZB, the occlusion mode writes this frame's history and the
  colour lists as before; phase 2 ranks visible-and-not-in-phase-1 into
  `phase2_ids`/`phase2_commands`. One phase-2 depth pass draws CLOD phase 2
  and MultiMesh phase 2 (`RenderListParameters::depth_phase2_only`, its own
  render-pass uniform set for the phase-2 IDs).
- Correctness: phase-1 depth is a subset of the final depth (the HZB is
  conservative), and visible is a subset of phase 1 plus phase 2, so the
  colour pass's depth-equal test finds every visible instance.
- `multimesh_depth_instances` statistic (phase 1 plus phase 2).

# Proof (2026-11-24, Linux, RTX 3060, Vulkan)

`multimesh_occlusion.tscn` part E: two-pass depth draws 55 of 691
instances in the wall view; static views, 30 frames alternating between two
views (each frame gets the other view's history) and TAA all equal the
reference exactly; two-pass off equals too.

`hzb_bench.tscn` (street grid, 3,744 MultiMesh props), new mode
`two_pass+mm2` against `two_pass+mm` (colour-only MultiMesh occlusion):

| View | GPU p50 | Depth p50 |
| --- | --- | --- |
| street | 4.905 to 4.405 ms (-10%) | 2.785 to 2.267 ms |
| corner | 4.035 to 3.557 ms (-12%) | 1.915 to 1.576 ms |
| aerial | 7.573 to 7.035 ms (-7%) | 3.677 to 3.256 ms |

`multimesh_two_pass.tscn` (valley, fixed views): depth draws 2236/2478,
4058/5250, 3917/4689 instances, but GPU time 101-107% of one pass. The
valley's depth pre-pass is dominated by terrain and grass, not MultiMesh
trees (phase-1 interval 1.197 to 1.172 ms), so the selection passes and
the phase-2 pass's extra indirect draws cost more than they save.

The valley does not repeat exactly along a moving camera path even with
one pass (9 of 12 sampled frames differ between two identical runs), so
image equality is tested in the controlled scene only.

# Next

- Cut the open-scene cost: phase 2 re-issues every GPU-culled MultiMesh
  draw (mostly zero instances); skip it when no instance became visible.
- Same scheme for GPU path instances and CPU list meshes (they still draw
  everything in the pre-pass).
