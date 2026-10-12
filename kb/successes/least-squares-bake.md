---
type: Success
title: Least-squares baking
description: Solving baked probe values against a half-spacing bake, with the renderer's own interpolation weights, roughly halved the baked GI error against a finer bake.
tags: [success, ddgi, baking, quality]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: cod
    resource: /research/cod-iw-precomputed-lighting.md
    title: Precomputed lighting in CoD - Infinite Warfare
---

# What worked

Call of Duty bakes its light grid by solving for probe values whose
interpolation matches many samples, with a smoothness regularizer.[^cod]
Here the samples are the probes of a second bake at half the spacing,
which already exist in the engine, so no new tracing code was needed.

| Scene | Plain bake | Least squares |
| --- | --- | --- |
| Interior, mean difference to a 0.25 m reference bake | 1.341 | 0.738 |
| Room, 2 cascades | 0.721 | 0.471 |

# What made it work

- **Mirror the renderer exactly in the solver:** the same weights,
  visibility test, crush and square-root blending. The equations then
  describe what surfaces really see.
- **Reuse an existing capability as the sample source** (a finer bake)
  instead of writing a new tracer.
- **Validate against an independent finer bake.** Comparing against the
  fine data the solver used would only show that the fit worked.
- **Keep probes inside geometry out of the solve** (Enemies: dilating
  into them creates new leaks).

[^cod]: Precomputed lighting in CoD - Infinite Warfare
