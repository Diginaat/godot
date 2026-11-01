---
type: Decision
title: Reflection architecture - PPR and normalized probes
description: Planar reflections by pixel projection rather than a mirrored re-render; probe light changes by DDGI normalization rather than relit G-buffer probes; Godot's probes extended rather than replaced.
tags: [reflections, decision, planar, probes, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
sources:
  - id: ppr
    resource: /research/pixel-projected-reflections.md
    title: Pixel-projected reflections
  - id: norm
    resource: /research/cubemap-normalization.md
    title: Cubemap normalization
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei relit probes
---

# Decision

1. **Planar: pixel-projected reflections.** A mirrored camera render
   (Unreal, Unity HDRP) sees off-screen objects but costs a full scene
   render per reflector, and its triangle and draw call cost doesn't
   scale with resolution. PPR is exact for on-screen content, costs two
   screen passes regardless of the scene, has no noise and no history,
   and needs only 32-bit image atomics.[^ppr] Off-screen content falls
   back to probes and the sky, as for SSR.
2. **Probes: extend Godot's ReflectionProbe.** It already has box
   projection, blending, filtering and the clustered lookup; rewriting
   would duplicate it. Its problems were cost (realtime probes render
   everything every frame), memory (all atlas layers up front) and static
   light.
3. **Light changes: normalization, not relighting.** Relit probes follow
   every light but need a G-buffer per probe and a light loop.[^yotei]
   Normalization costs one texture read per probe and pixel, and DDGI
   already gives the irradiance at every pixel.[^norm]

# Rejected or left for later

- Hardware ray traced reflections: not vendor independent enough.
- A mirrored-camera mode for mirrors facing the camera: possible later as
  an opt-in, budgeted mode (REFLECTIONS.md step 8).
- DDGI light inside probe captures: step 9.

[^ppr]: Pixel-projected reflections
[^norm]: Cubemap normalization
[^yotei]: Ghost of Yotei relit probes
