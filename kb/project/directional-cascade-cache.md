---
type: Finding
title: Far directional shadow cascades kept between frames
description: The far cascades of a directional light are rendered every 2nd and 4th frame, staggered, and reused in between while the camera's slice still fits inside the region they cover.
tags: [rendering, shadows, performance]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-08T23:00:00+01:00 }
---

# What it does

`rendering/lights_and_shadows/directional_shadow/far_cascade_update_interval`
(default 4) sets how often the far cascades render. With four splits the
last cascade renders every 4th frame and the third every 2nd, on alternating
frames, so no frame renders both. With two splits the second renders every
2nd frame. The near cascades render every frame.

`RendererSceneCull::_light_instance_setup_directional_shadow()` keeps, per
light and cascade, what the last render wrote: the light-space region its
casters and map cover, and the shadow parameters shading needs. A cached
cascade covers a margin of 10% of its radius around the slice
(`DIRECTIONAL_CASCADE_CACHE_MARGIN`). It is reused while:

* this frame's slice (its casters' light-space bounds, soft shadow spread
  included, and the receivers' depth) lies inside the cached region, a
  quarter margin in from its edges;
* the key matches: atlas generation, light version, light rotation, shadow
  index, split count, the camera's visible layers, atlas texel size.

A reused cascade is not culled and not drawn; its parameters are set again
from the cache. Cacheable cascades skip the light culler's tighter caster
culling, which fits only the current slice.

The map only depends on the region it covers, so it does not matter which
viewport rendered it: a second viewport reuses it when its slice fits, and
otherwise renders it and becomes the new owner. Directional passes now clear
only their own atlas region (Forward+ and Mobile); the RD renderers opt in
through `RendererSceneRender::directional_shadow_cache_generation()`, which
changes when the atlas is reallocated. Compatibility returns 0 and keeps
rendering every cascade.

# Evidence

Valley, RTX 3060, Vulkan, whole-frame GPU time
(`devtools/test/gpu_intervals.ps1`, two runs each, interval 1 against 4):

| View | Shadows | Frame |
| --- | --- | --- |
| Held at route time 27 (`--view-time=27`) | 3.53 to 2.03 ms | 7.79 to 6.30 ms |
| Fly-through, frame-locked (`-FixedFps`) | 2.53 to 1.41 ms | 5.07 to 4.00 ms |

Per cascade at the held view: 0.42, 0.68, 1.13 and 1.29 ms before. A cached
render costs 1.26 ms for both far cascades (162 to 210 and 317 to 372
elements: margin and no tight culling), paid every 2nd and 4th frame. The
worst frame drops from 3.5 to 2.3 ms of shadows.

Images: frame-locked screenshots of the moving fly-through differ from
interval 1 at 0.1-0.7% of pixels by more than 8 levels (HUD numbers
included), against 0.02-0.04% between two runs with the cache on. The
differences are thin edges of far trees: the far cascades are 10% coarser,
and their wind sway is up to 3 frames old. The valley smoke (day on Vulkan
and D3D12, night), the 18 `large_world` fixtures, the dense forest A/B and
the smoke suite pass.

# Limits

* Moving objects in the far cascades update their shadows every 2nd or 4th
  frame.
* A sun that turns every frame (time of day) renders every cascade every
  frame: any change of the light's rotation starts over.
* Two viewports with different cameras each frame (split views, mirror
  passes) rarely fit each other's slices and render every frame, as before.
