---
type: Reference
title: "Enemies: precomputed indirect diffuse (adaptive probe volumes)"
description: Unity's adaptive probe volumes as used in the Enemies demo; brick placement, invalid probe handling and leak weighting.
tags: [research, probes, baking, light-leaks]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://advances.realtimerendering.com/s2022/index.html
    title: Ciardi et al., Enemies, SIGGRAPH 2022 Advances in Real-Time Rendering course
---

# Summary (probe lighting part)

- **Placement**: bricks of 4x4x4 probes, subdivided where an SDF of the
  scene says geometry is near; corners shared between subdivisions.
  Streamed per cell; per-cell indirection to an SH pool.[^talk]
- **SH storage**: L0 at high precision, L1/L2 stored relative to L0 at low
  precision (after Frostbite).
- **Seams** between resolutions: animated interleaved gradient noise on
  the sample position in the tangent frame; TAA hides the noise.
- **Invalid probes** (inside geometry, scored by backfaces seen at bake):
  virtual offset (bake from the nearest way out, sample at the grid
  position), then dilation from valid neighbours (inverse squared distance)
  for the rest. Both can create new leaks.
- **Runtime leak weighting**: normal bias and view bias (keep small), a
  geometry weight, and a **validity mask**: 8 bits per probe, one per
  probe of the 2x2x2 sampling neighbourhood, used to push the trilinear
  sample away from invalid probes (one extra 8-bit load). Validity is
  restored for a neighbourhood with no occluder in it.
- **Touch-up volumes**: artist boxes that mark probes invalid, to virtually
  thicken thin walls (7 cm ceiling example); rarely needed.

# For this fork

- We already skip inside probes and weight by backface and Chebyshev
  visibility; the validity mask is mainly a cost saving for systems
  without distance moments.
- **Seam dithering** for our cascade edges, and **touch-up / interior
  volumes** for thin walls, are worth trying: see the
  [proposals](/ddgi/improvements/index.md).
- Relative SH storage is relevant to baked data size.

[^talk]: Enemies, SIGGRAPH 2022 Advances
