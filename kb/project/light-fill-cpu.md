---
type: Finding
title: Render-thread cost of many lights halved
description: At 4096 omni lights the render-thread CPU time per frame was 0.89 ms. Most of it went to a pow() per color channel per light, a comparison sort of 24-byte structs, and one cluster draw per element. Caching the linear color, a radix sort and instanced cluster draws bring it to 0.47 ms with the same image. Instancing needed a fix in cluster_render.glsl, which had assumed one element per draw.
tags: [lighting, clustered, forward-plus, cpu, render-thread]
status: stable
sources:
  - id: bench
    title: devtools/test/light_bench.ps1 (misc/light_bench_project), 2026-11-27, branch dev-renderer-next, RTX 5070, 1920x1080
  - id: culling
    title: Wihlidal, Optimizing the Graphics Pipeline with Compute, GDC 2016 / Drobot, Improved Culling for Tiled and Clustered Rendering, SIGGRAPH 2017 (the subgroup merge in cluster_render.glsl)
    url: https://advances.realtimerendering.com/s2017/2017_Sig_Improved_Culling_final.pdf
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

`light_bench` (omni lights over 1,600 boxes, `max_clustered_elements`
4096) measured 0.888 ms of render CPU per frame at 4096 lights, against
0.069 ms with none. Temporary timers (removed) split the time at 4096
lights; the per-light timers add about 0.08 ms of their own:

| Phase | ms |
| --- | --- |
| `LightStorage::update_light_buffers()`, gather | 0.033 |
| Depth sort (`SortArray` of `LightInstanceDepthSort`) | 0.149 |
| Fill of `LightData` (with `ClusterBuilderRD::add_light()`) | 0.428 |
| Upload | 0.016 |
| `bake_cluster()` (one draw per element) | 0.110 |
| Scene cull and shadow setup | 0.11 |

In the fill, `Color::srgb_to_linear()` costs three `pow()` calls per
light per frame for a value that changes only when the color is set.

# Changes

- `Light::linear_color`, set in `light_set_color()`. The fills of
  directional and positional lights read it instead of converting.
- `LightStorage::_sort_lights_by_depth()`: 64 lights or more are sorted
  by a stable LSD radix sort. The key is the depth's float bits (camera
  distance, never negative, so it orders as an unsigned integer) above
  the light's index, sorted in four 8-bit passes. Fewer lights still use
  `SortArray`.
- `bake_cluster()` draws consecutive elements of the same proxy mesh
  (sphere, cone, box) as one instanced draw. The vertex shader already
  added `gl_InstanceIndex` to `base_index`; upstream drew one instance
  per draw.
- `cluster_render.glsl`: with helper-lane checks (the default), lanes
  were merged by cluster only, and the first lane wrote the element bit
  for all of them. That assumed one element per draw. The first
  instanced version lost bits: 6 to 35 pixels differed by up to 12/255.
  Lanes now merge by cluster and element. The depth bits are merged per
  group too: upstream merged them across the whole subgroup, so an
  element could get depth slices from other clusters. Lighting is the
  same, and the opaque pass is a little faster.

# Measured

Render CPU and GPU frame p50, Vulkan, limit 4096, ms:

| Lights | CPU before / after | GPU before / after | Opaque pass before / after |
| --- | --- | --- | --- |
| 0 | 0.069 / 0.068 | 0.239 / 0.238 | 0.095 / 0.094 |
| 512 | 0.137 / 0.114 | 0.420 / 0.415 | 0.226 / 0.223 |
| 1024 | 0.252 / 0.193 | 0.587 / 0.583 | 0.361 / 0.354 |
| 2048 | 0.474 / 0.284 | 0.942 / 0.932 | 0.632 / 0.618 |
| 4096 | 0.888 / 0.470 | 1.697 / 1.675 | 1.212 / 1.187 |

The frame of every light count (`light_bench.gd --image-dir=<dir>`) is
bit-identical before and after.
