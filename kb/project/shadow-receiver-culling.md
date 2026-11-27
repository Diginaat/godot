---
type: Decision
title: Receiver culling of directional shadow casters
description: Directional cascades rendered every frame skip GPU-path casters whose shadow reaches no surface the camera sees. A 64 x 64 light-space receiver mask per cascade comes from the final depth pre-pass in 4 x 4 pixel blocks, plus the bounds of visible transparent instances; adaptive (off while it culls under a million triangles). Same image. City GPU frame 17-22% faster with every cascade rendered per frame; neutral elsewhere, also in the default far-cascade cache and the valley.
tags: [shadows, culling, gpu-driven, rendering]
status: stable
sources:
  - id: bittner
    title: Bittner, Mattausch, Silvennoinen, Wimmer, Shadow Caster Culling for Efficient Shadow Mapping, I3D 2011 (receiver masks from the camera depth, casters tested against them)
    url: https://www.cg.tuwien.ac.at/research/publications/2011/bittner-2011-scc/
  - id: lightculler
    title: Upstream RenderingLightCuller (casters clipped to the light-extruded camera slice; already in Godot)
  - id: cache
    title: Far directional cascades kept between frames (this KB)
    url: /project/directional-cascade-cache.md
  - id: bench
    title: misc/large_world_test_project/shadow_receivers.tscn -- --bench, 2026-11-27, branch dev-renderer-next, RTX 5070, 1920x1080
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Problem

A directional cascade draws every caster inside its light frustum. The
scene cull already clips that frustum to the camera slice swept toward the
light (upstream `RenderingLightCuller`), but it knows nothing about
occlusion: in a city, the props and buildings behind the first row of
houses cast shadows onto streets nobody sees. Roadmap item G4.

# Design

`shadow_receivers.glsl` builds one mask per cascade the GPU path draws
this frame, after the depth pre-pass (phase 2 included) and before the
shadow cull in `_gpu_instances_shadow_flush()`:

- A 64 x 64 grid over the cascade's shadow map (32 texels of a 2048
  cascade per cell) plus two 8 x 8 coarse grids. A cell holds the farthest
  light distance of any receiver in it (`atomicMax` on the float bits, with
  a plain load first: most cells already hold a larger value, which cut the
  build from 0.10 to 0.03 ms in the aerial view).
- One thread per 4 x 4 pixel block reads the block's nearest and farthest
  depth. The part of that depth range inside the cascade's view depths
  (`RenderShadowData::camera_receiver_near/far`, with split blending the
  previous cascade's range too) is a frustum piece; its light-space
  rectangle, dilated by one cell (more than any filter kernel or normal
  offset) plus the soft-shadow blocker search, takes the largest light
  distance of its 8 corners. 16-pixel tiles smeared receivers over the
  space between a near facade and the far street (almost nothing culled);
  4 x 4 blocks keep that local.
- Empty pixels (sky) mark nothing. Visible transparent instances
  (`RENDER_LIST_ALPHA`, not in the depth pre-pass) mark their AABB
  (`MODE_BOXES`). With volumetric fog, or more than 4096 transparent
  instances, each block marks the whole view ray from the cascade's near
  depth (fog samples the shadow in front of surfaces).
- Large footprints (over 64 fine cells) mark only coarse cells. The caster
  test (`receivers_reachable()` in `shadow_receivers_inc.glsl`, called by
  `gpu_instance_cull.glsl` in `MODE_SHADOW_BATCH`) projects the member's
  world AABB into light space and keeps it if any cell of its footprint
  holds a receiver at least as far from the light as the box's nearest
  point (minus two cells of depth margin). Non-finite values keep the
  caster.

Not culled: cascades kept between frames (`far_cascade_update_interval`,
by default cascades 2 and 3 of 4): a cached cascade must hold the casters
of the receivers of later frames. Spot and omni shadows, the CPU list's
casters (passes under `gpu_instances/shadow_min_casters`), MultiMesh and
CLOD casters are not tested. No mask without the HZB conditions (no MSAA,
no custom fragment depth or stencil, single view) or with more than 64
cascades. The validation check (`gpu_instances/validate`) turns the mask
off for the pass it checks, so its counts stay exact.

Adaptive: the cull counts the casters it tested and the primitives the
masks culled (stats words 2 and 3, read back with the shadow statistics).
When a view's last masks culled fewer than 2^20 primitives (about what the
masks cost to build at 1080p), it builds none for 30 frames, then tries
again. Culled fractions mislead: in the default city street view the masks
culled 65% of the 429 casters of the near cascades and still lost 0.02 ms.

Setting: `rendering/lights_and_shadows/directional_shadow/receiver_culling`
(default on). The image is the same with it on or off.

# Measured

City of 24 x 24 blocks, 14,400 objects (buildings and 576-triangle props),
sun with 4 cascades to 300 m, `shadow_receivers.tscn -- --bench`, RTX 5070,
Vulkan, 1920x1080. GPU frame p50 (and shadow stage p50), ms, two
alternating repeats of 240 frames, final (adaptive) version:

| View | Default (far cascades cached), off / on | Every cascade per frame, off / on |
| --- | --- | --- |
| street | 0.669 / 0.669 | 0.764 / 0.609 (shadows 0.387 / 0.232) |
| rooftop | 0.679 / 0.678 | 0.830 / 0.647 (shadows 0.465 / 0.281) |
| aerial | 1.136 / 1.136 | 1.203 / 1.203 |
| fog (street, volumetric fog) | 0.747 / 0.748 | 0.841 / 0.695 (shadows 0.388 / 0.240) |

Mask build when used: 0.035-0.049 ms (fog marks whole view rays). Before
the plain load in front of each `atomicMax` the aerial build took 0.097 ms
and made that view 4% slower; before the adaptive rule the default street
view lost 0.02 ms.

With every cascade rendered per frame, receiver culling beats the default
cache in the street (0.609 against 0.669 p50, 0.775 against 0.930 p95)
and the rooftop (0.647 against 0.679), without the cache's delayed far
shadows of moving objects. The valley (`forest_bench.ps1`, open terrain)
is neutral with receiver culling either way, but rendering every cascade
per frame costs it 0.15-0.31 ms, so the cache stays the default
(`far_cascade_update_interval` 4).

Valley, GPU p50 ms, receiver culling off / on:

| View, mode | Default interval 4 | Interval 1 |
| --- | --- | --- |
| 0, source | 1.049 / 1.051 | 1.202 / 1.215 |
| 1, source | 1.965 / 1.973 | 2.276 / 2.277 |
| 2, source | 1.463 / 1.465 | 1.631 / 1.637 |
| 1, culling | - | 2.161 / 2.160 |

# Tests

`devtools/test/large_world.ps1` runs `shadow_receivers.tscn` (check mode,
every cascade per frame): street, rooftop, aerial and fog views, a
translucent pane over the street; the images with and without receiver
culling must not differ by more than 2/255 in any pixel
(`SHADOW_RECEIVERS_TEST_PASS`; all views are bit-identical except fog,
77.7 dB, whose temporal filter differs below that).

# Remaining work

- An adaptive cascade cache: a far cascade that receiver culling thins out
  a lot could render every frame instead of being cached (the city), while
  open terrain keeps the cache.

- Cull the CPU list's casters (needs the mask on the CPU a frame late,
  which is not conservative under camera motion) and MultiMesh shadow
  instances (same test in the MultiMesh shadow cull).
- Spot lights: a mask in the spot's perspective space.
