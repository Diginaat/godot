---
type: Proposal
title: Least-squares baking with a smoothness regularizer
description: Bake probe values so that the interpolated light matches dense samples between probes, with a penalty on differences between mutually visible probes - less interpolation error and noise in baked DDGI.
tags: [ddgi, baking, quality, proposal]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: cod
    resource: /research/cod-iw-precomputed-lighting.md
    title: Precomputed lighting in CoD - Infinite Warfare
---

# Result (2026-10-12, branch `dev-ddgi-quality`)

Built as `DDGIVolume.bake_least_squares` (default on),
`scene/3d/ddgi_bake_solver.cpp`. Instead of surface samples, the samples
are a second bake with half the probe spacing; the system per cascade,
texel and channel is solved by conjugate gradients on the CPU. Against an
independent half-spacing bake, the error of the baked GI buffer drops
from 1.34 to 0.74 levels in the interior and from 0.72 to 0.47 in the
room (2 cascades); bake time 0.5 -> 6.0 s for the interior. Details:
DDGI.md, step 24. Not tuned yet: smoothness and prior weights (0.05
each).

# Before

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
