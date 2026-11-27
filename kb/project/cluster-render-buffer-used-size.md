---
type: Finding
title: The cluster render buffer sized by the limit cost 0.27 ms per frame
description: ClusterBuilderRD cleared and strode its intermediate render buffer by max_clustered_elements times five element types, every frame, whatever the light count (172 MB at 1080p with the limit at 4096). Laying it out for the frame's used elements removes the cost; the GPU frame at limit 4096 with 128 lights went from 0.583 to 0.309 ms.
tags: [lighting, clustered, forward-plus, bandwidth]
status: stable
sources:
  - id: bench
    title: devtools/test/light_bench.ps1 (misc/light_bench_project), 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

`ClusterBuilderRD::bake_cluster()` rasterizes every clustered element
(omni, spot, area, decal, reflection probe) into an intermediate buffer:
per cluster tile, one tag bit per element and one depth-slice word per
element. The buffer's stride was `render_element_max / 32 + render_element_max`
words, with `render_element_max = max_clustered_elements * 5`, and the
whole buffer was cleared each frame. At 1920x1080 (60 x 34 tiles of 32
pixels):

| `max_clustered_elements` | Buffer cleared per frame |
| --- | --- |
| 512 (default) | 21.5 MB |
| 4096 | 172 MB |

So raising the limit for a many-light scene cost about 0.27 ms of GPU
time per frame on an RTX 5070 before any light was drawn.

# Fix

The layout is per frame: the stride uses this frame's element count
rounded up to 32 (`used_elements / 32 + used_elements`), set in the state
uniform for `cluster_render.glsl` and the push constant for
`cluster_store.glsl`, and the clear covers only `tiles * stride`. The
allocation stays at the limit. Nothing else reads the intermediate
buffer; the final cluster buffer the scene shaders read is unchanged.

# Measured

`light_bench` (1600 boxes, omni lights of range 6 added in stages,
1920x1080, RTX 5070, 240 frames per stage, medians, ms GPU frame).
Vulkan, before -> after, separate runs of the same scene:

| Limit | Lights | Before | After |
| --- | --- | --- | --- |
| 512 | 128 | 0.317 | 0.307 |
| 512 | 4096 (512 drawn) | 0.515 | 0.507 |
| 4096 | 0 | 0.244 | 0.243 |
| 4096 | 128 | 0.583 | 0.309 |
| 4096 | 512 | 0.724 | 0.455 |
| 4096 | 1024 | 0.890 | 0.630 |
| 4096 | 2048 | 1.232 | 0.978 |
| 4096 | 4096 | 1.942 | 1.715 |

D3D12 after (no before run): limit 4096 with 128 lights 0.481 ms, with
4096 lights 1.500 ms; limit 512 with 128 lights 0.473 ms.

# Tests

Large-world suite (24 tests, including `light_cap`) and smoke tests on
Vulkan and D3D12.

# Next

At 4096 lights the opaque pass is 1.17 ms of the 1.72 ms frame (shading),
the rest mostly cluster rasterization and the store pass; the render
thread spends 0.84 ms (light buffer fill). Those are the next many-light
steps (roadmap G7).
