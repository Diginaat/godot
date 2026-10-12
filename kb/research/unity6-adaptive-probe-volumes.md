---
type: Reference
title: Unity 6 URP Adaptive Probe Volumes (deep dive)
description: Unity's per-pixel probe system - 4x4x4 bricks at 1/3/9/27 m, automatic placement, virtual offset, baked sky occlusion times dynamic sky color, lighting scenario blending, disk streaming.
tags: [research, gi, probes, adaptive-density, sky-occlusion, streaming]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: article
    resource: https://gamedevllm.com/en/unity-6-urp-adaptive-probe-volumes-deep-dive-en/
    title: Adaptive Probe Volumes in Unity 6 URP - A Deep Dive (gamedevllm.com, 2026-04-30)
---

# Summary

- **Bricks**: probes come in bricks of 4x4x4 = 64 probes. Brick spacing is
  hierarchical, 1 / 3 / 9 / 27 m, chosen from geometry density: small
  bricks near detailed geometry, large ones over open terrain.[^article]
- **Placement** is automatic from renderers that contribute GI; artists
  override spacing globally or with local volumes. No per-probe editing.
- **Sampling** is per pixel from the 8 nearest probes (legacy light probe
  groups gave one value per object).
- **Virtual offset** pushes probes out of walls and away from surfaces.
- **Sky occlusion**: a baked per-probe value of how much sky reaches it.
  At runtime, sky light = dynamic sky color x baked occlusion, so time of
  day changes the sky light without a rebake. An optional sky direction
  isn't interpolated between probes and can show seams.
- **Lighting scenarios**: several baked datasets (lighting states), switched
  or blended at runtime.
- **Streaming**: data is split into cells (groups of bricks) on disk; only
  cells in the view frustum are loaded, asynchronously.
- Needs compute shaders; can't be mixed with legacy light probe groups. No
  performance or memory numbers are given.

# For this fork

- The brick hierarchy is the shape [adaptive probe density](/ddgi/improvements/adaptive-density.md)
  would take; see also [Enemies](/research/enemies-adaptive-probe-volumes.md),
  which uses the same system.
- Baked sky occlusion x dynamic sky color is what lets a baked DDGI volume
  follow time of day cheaply. Our realtime DDGI traces the sky directly;
  for `BAKE_MODE_BAKED`, storing sky visibility separately from the rest
  of the light would allow a time-of-day change without probe updates.
- Scenario blending and cell streaming fit [compressed baked data](/ddgi/improvements/compressed-baked-data.md).

[^article]: Adaptive Probe Volumes in Unity 6 URP - A Deep Dive
