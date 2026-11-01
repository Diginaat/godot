---
type: Overview
title: Reflections with DDGI
description: Reflection probes with a shared face budget, DDGI normalization and an atlas that grows on demand; planar reflections by pixel projection. Vendor independent, independent of DDGI.
tags: [reflections, probes, planar, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
sources:
  - id: runbook
    resource: ../REFLECTIONS.md
    title: REFLECTIONS.md
---

# What is built

- **Probes** keep Godot's `ReflectionProbe` (box projection, octahedral
  atlas, GGX filtering, clustered lookup). Added: a shared budget of
  cubemap faces per frame with a priority (new or moved first, then
  realtime probes by age over distance), a global toggle, DDGI
  normalization of the reflections, DDGI's diffuse light kept over the
  probe ambient, and an atlas texture that grows from 4 layers by
  doubling (copying captured layers).
- **Planar reflections**: `PlanarReflector3D` rectangles; two compute
  passes mirror the screen across them (pixel-projected reflections),
  composited like SSR before the BRDF's split-sum term.

# Numbers (RTX 3060, 1080p)

Planar 0.34 ms (0.10 ms half resolution); a realtime probe 0.82 ms per
frame with 2 faces per frame instead of 3.20 ms; atlas 47 MB for one probe
instead of 512 MB. Details and conditions in the runbook.[^runbook]

# Limits

Planar reflections only reflect what is on screen; probe captures don't
contain DDGI's light (normalization fixes the level only); Forward+ only
for planar and normalization; tested on NVIDIA (Vulkan, Direct3D 12) only.

[^runbook]: REFLECTIONS.md, "Measurements" and "Known limitations".
