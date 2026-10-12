---
type: Proposal
title: Least-squares baking with a smoothness regularizer
description: Bake probe values so that the interpolated light matches dense samples between probes, with a penalty on differences between mutually visible probes - less interpolation error and noise in baked DDGI.
tags: [ddgi, baking, quality, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: cod
    resource: /research/cod-iw-precomputed-lighting.md
    title: Precomputed lighting in CoD - Infinite Warfare
---

# Today

The bake traces each probe and stores its own light (many rays, averaged).
What surfaces see is the trilinear, visibility-weighted interpolation of 8
probes, which can be off between probes even when every probe is right:
leaks, dark seams and blotches where probes see different things.

# Proposal

Call of Duty bakes its light grid by least squares:[^cod]

1. Sample the light at many points between the probes (near surfaces
   especially).
2. Solve for the probe values whose *interpolated* light (the same weights
   and visibility test the renderer uses) matches those samples.
3. Add a Laplacian regularizer: penalize differences between neighboring
   probes that see each other. This smooths and denoises, so each sample
   can use fewer rays.

For DDGI the unknowns are the octahedral irradiance texels of each probe.
The system is sparse (each sample touches 8 probes) and can be solved
iteratively on the GPU (conjugate gradient), per texel direction.

# Measure

Bake time; error of the baked GI buffer against the path tracer at
surface points between probes (interior, thin walls); seams and blotches
in screenshots; noise with fewer rays per sample.

[^cod]: Precomputed lighting in CoD - Infinite Warfare
