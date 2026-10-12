---
type: Reference
title: "SDFDDGI: Signed Distance Fields Dynamic Diffuse Global Illumination"
description: DDGI variant tracing an SDF primitive scene; SDF-based probe relocation and per-pixel visibility, Contact GI built on GTAO.
tags: [research, ddgi, sdf, ambient-occlusion]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: paper
    resource: https://arxiv.org/abs/2007.14394
    title: Hu, Yip, Alonso, Gu, Tang, Jin. SDFDDGI. arXiv 2007.14394, 2020
---

# Summary

Replaces DDGI's hardware rays with sphere tracing against a scene made of
analytic SDF primitives (Sponza: 43 primitives in 8 clusters, about 4 KB),
clustered for culling and held in L1 cache.[^paper]

- **Probe placement**: if the SDF at a probe is below a threshold, move it
  by gradient descent; if it moved far since last frame, drop its history
  and give it more rays.
- **Update scheduling** by distance and direction to the camera.
- **Multi-bounce** from last frame's probes, with fewer samples and a 0-1
  gain on the bounce term to speed up response.
- **Visibility**: replaces DDGI's distance moments with an SDF soft-shadow
  trace per pixel and probe. Cost cut from 8 tests per pixel to 0.25 by a
  min/max checkerboard half-resolution depth, one pixel per 2x2 per frame,
  de-duplicating probes in 4x4 blocks, reprojection and neighbourhood
  clipping. Claims no leaks even with thin walls.
- **Contact GI**: during GTAO, sample the lighting at the occluding points
  and merge it with the probe GI, so occluded detail gets bounce light
  instead of plain darkening.
- **Cascades** blended with mean value coordinates.
- Timings (RTX 2080 Ti, 1080p, Sponza): 1.67 ms total (probes 0.70, Contact
  GI 0.41, shading 0.56) vs RTXGI 3.98.

# For this fork

- Its min/max checkerboard depth downsampling (alternate low resolution
  pixels take the nearest or farthest depth of their area) is used by the
  [low-resolution apply](/ddgi/improvements/low-res-apply-directional.md).

- Ours traces real geometry in hardware; the SDF scene needs hand-made
  primitives (the paper's own limitation). Not a replacement.
- **Contact GI** fits the XeGTAO work: see
  [proposal](/ddgi/improvements/contact-gi.md).
- History rejection on large relocation matches our relocation; check we
  reset when a probe moves far.

[^paper]: SDFDDGI, arXiv 2007.14394
