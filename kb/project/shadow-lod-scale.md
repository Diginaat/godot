---
type: Finding
title: Coarser mesh LODs for shadow casters
description: Shadow passes multiply the mesh LOD threshold (default 4x), so casters use coarser LODs than the camera sees; 0.4 ms in the valley with no visible change.
tags: [rendering, shadows, lod, performance]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-09T01:00:00+01:00 }
---

# What it does

`rendering/mesh_lod/lod_change/shadow_threshold_scale` (default 4.0)
multiplies `rendering/mesh_lod/lod_change/threshold_pixels` in every shadow
pass (directional and positional lights, Forward+ and Mobile:
`_render_shadow_append()`). LOD distances are still measured from the main
camera, so a caster near the camera keeps its detailed LOD; distant casters
render coarser ones. `1.0` uses the camera's LODs (upstream behaviour).

# Evidence

Valley, held view (route time 27), RTX 3060, Vulkan, whole-frame GPU
(`gpu_intervals.ps1`), with the far cascade cache on:

| Scale | Shadows | Frame |
| --- | --- | --- |
| 1 | 2.02 ms | 5.94 ms |
| 2 | 1.81 ms | 5.74 ms |
| 4 | 1.62 ms | 5.55 ms |
| 8 | 1.32 ms | 5.25 ms |

Images (valley smoke, 8 fixed views, HUD masked): two runs at scale 1
differ at 0-0.34% of pixels by more than 8 levels; scale 4 at 0.01-2.2%,
scale 8 at 0.09-3.0%. At scale 4 the differences are speckles in grass
self-shadowing and thin far tree edges; side-by-side crops look the same.
The `large_world` fixtures, smoke tests and valley smoke (Vulkan, D3D12)
pass.

# Tried without effect (2026-11-09)

* Generating `GrassField3D` patches in rings outward from the camera (near
  blades first in the buffer) left the alpha-tested depth pre-pass at 1.21
  ms in a real level: the overdraw is inside each clump mesh, and order
  between clumps does not matter.
* Recording the ~12 frame-level GPU geometry stage timestamps only while
  profiling changed nothing measurable (valley bench, +-0.06 ms). Only
  per-object timestamps cost (149 voxel fields: 3.1 ms).
* The weather sky's radiance runs in Godot's incremental mode; its 0.23 ms
  is upstream's per-frame filtering, not a full re-render.
