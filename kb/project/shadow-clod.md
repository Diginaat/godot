---
type: Decision
title: Cluster LOD in shadow passes
description: Each directional cascade, spot light and omni cube face selects its own CLOD cut with the error in shadow-map texels; one compute batch per shadow batch, compact streams. Shadow stage 9-11x faster, GPU frame 2.2-2.6x in shadow_clod_bench; finest-group images equal the source.
tags: [geometry, clod, shadows, rendering, virtual-geometry]
status: stable
sources:
  - id: nanite
    title: Karis, Stubbe, Wihlidal, A Deep Dive into Nanite Virtualized Geometry, SIGGRAPH 2021 Advances in Real-Time Rendering (shadow views select their own cut at shadow-map resolution)
    url: https://advances.realtimerendering.com/s2021/index.html
  - id: compact
    title: Compact CLOD command streams (this KB)
    url: /project/compact-clod-commands.md
  - id: bench
    title: devtools/test/shadow_clod_bench.ps1, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Problem

CLOD cut only the camera passes. Every shadow pass drew CLOD meshes from
their full source (or shadow mesh) index buffer, and the shadow lists
batched instances of one mesh into one instanced draw. In `hzb_bench` the
sun was left unshadowed because the source shadow draws "would dominate
the frame". With 144 dense meshes (9.5 M source triangles), a 4-split sun,
a spot and an omni cube light, shadows took 0.86-0.93 ms of a 1.25-1.43 ms
frame.

# Decision

- A shadow pass is a view like any other for the selection kernel
  (`mesh_clod_select.glsl` unchanged): frustum planes of the pass, the
  light as eye, and `|P[1][1]| * height / 2` as the projection scale. For
  an orthographic cascade that is texels per world unit, so the error is
  bounded in **shadow-map texels** at every distance; for spot and cube
  faces it is texels at distance 1, divided by the group's depth as for
  the camera[^nanite].
- The near plane is dropped: directional casters between the light and the
  cascade are pancaked onto it, and nothing it removes would be drawn.
- `rendering/geometry/cluster_lod/shadows` (default on) and
  `rendering/geometry/cluster_lod/shadow_error_texels` (default 1.0, an
  error well inside the default normal bias).
- `_render_shadow_append()` fills the pass with CLOD surfaces unbatched,
  and `_mesh_clod_shadow_append()` gives every eligible element (lod 0,
  camera eligibility plus a plain shadow material) its input and command
  offset in one renderer-wide batch. `_render_shadow_process()` selects
  all passes of the batch in one compute list (`_mesh_clod_shadow_dispatch()`),
  `_render_shadow_end()` draws them from that buffer with the GPU count[^compact]
  (multi-draw records without indirect count; per-record draws without
  multi-draw).
- CLOD draws index the **source** vertices, also in shadow passes (a
  separate shadow mesh has its own numbering).
- Camera and shadow share `_mesh_clod_make_input()` and
  `_mesh_clod_dispatch()`.
- Dual-paraboloid omni passes keep source draws (not a linear projection).
- Shadow render info still counts source primitives and one draw per
  element.

Considered: reusing the camera's cut in shadow passes (wrong error: a far
camera cut is coarse where a near cascade is fine, and groups outside the
camera frustum cast visible shadows); a cut per light instead of per
cascade (far cascades would draw near-cascade detail).

# Measured

`shadow_clod_bench` (12 x 12 dense spheres of 65 k triangles, one shared
mesh and material, ground plane, sun 4 splits at 160 m, spot, omni cube,
1920x1080, RTX 5070, both modes alternating in one process, 2 repeats x
240 frames, medians)[^bench]. ms, source -> CLOD:

| View | GPU frame p50 | GPU frame p95 | Shadow stage p50 |
| --- | --- | --- | --- |
| Vulkan ground | 1.249 -> 0.483 | 1.593 -> 0.507 | 0.861 -> 0.095 |
| Vulkan aerial | 1.428 -> 0.594 | 1.773 -> 0.620 | 0.932 -> 0.098 |
| D3D12 ground | 1.412 -> 0.632 | 1.544 -> 0.640 | 0.858 -> 0.080 |
| D3D12 aerial | 1.526 -> 0.691 | 1.714 -> 0.697 | 0.926 -> 0.091 |

The shadow stage is `GPU Geometry Shadows` to `... End`, direct RD
timestamps captured while camera CLOD is active (cube passes, cascades,
spot; no GI in the scene).

# Tests

`shadow_clod.gd` in the large-world suite (Vulkan and D3D12), 960x540,
two views; the dense mesh has a shadow mesh with reversed vertex numbering:

- `shadow_error_texels` 0 (finest groups) against source shadows: at most
  0.05% of the pixels differ by more than 2/255. Measured 0.000-0.005%.
- Error 1 texel: PSNR at least 38 dB, at most 0.3% of the pixels off by
  more than 24/255. Measured 52.3-58.5 dB, 0.004-0.016%.
- Guard: the shadows change at least 1% of the image (2.1-5.4%).

`clod_raster.gd` pins `cluster_lod/shadows` off: it still shows that the
camera cut never reaches shadow passes.

# Limits and next

- Not run on GTX 10/16-series, AMD or Intel here.
- No HZB occlusion of groups inside shadow maps, and no receiver-aware
  caster culling (roadmap G4).
- Shadow passes of CLOD meshes still cost one element and one indirect
  call each (no batching across instances).
