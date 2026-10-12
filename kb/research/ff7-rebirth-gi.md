---
type: Reference
title: Final Fantasy VII Rebirth global illumination (analysis and mod)
description: A reverse-engineering look at FF7 Rebirth's baked ambient-cube probes and their reflection problems, and fixes - SH reconstruction, irradiance deconvolution, specular occlusion from direct shadows and bent normals.
tags: [research, gi, probes, reflections, specular-occlusion]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: post
    resource: https://frostbone25.github.io/p/ff7-rebirth-global-illumination/
    title: Matos, Final Fantasy 7 Rebirth Global Illumination (blog, 2026-07-17)
---

# Summary

- The game lights its large world with sparse baked probes instead of
  lightmaps. The probes use an **ambient cube**: a base color plus
  intensities along 6 axes, compact but with less color accuracy (like
  Half-Life 2's).[^post]
- The author converts them to order 2 SH at runtime, then **deconvolves**
  irradiance back to approximate radiance, so reflections can be filtered
  by roughness (band attenuation that grows with roughness).
- **Problems found**: cubemap reflections leak into shadowed areas and
  interiors, roughness-stretched reflection vectors give incoherent
  reflections, and screen-space reflections are too sharp for rough
  materials.
- **Fixes**:
  - suppress cubemap reflections where the direct light buffer is in
    shadow;
  - bent-normal specular occlusion;
  - stronger ambient occlusion;
  - a dim camera-attached ambient light to fill character shadows.
- Cost stayed the same or dropped: reflection shader 1.97 ms (was 2.16),
  GI sampling 1.64 ms (was 1.63), SSR about 0.38 ms (was 0.41).
- Conclusion: limited baked data, reconstructed carefully and with proper
  occlusion, gets close to games with richer specular data.

# For this fork

- [Specular occlusion for DDGI reflections](/ddgi/improvements/specular-occlusion.md):
  the DDGI apply pass also writes a reflection buffer. Reflection probes
  and sky reflections leaking into dark interiors is the same problem we
  partly addressed (sky and reflection probe light occluded in dark
  areas); bent normals from XeGTAO and the direct shadow are further
  inputs.
- Irradiance deconvolution and roughness-filtered SH: a way to get rough
  reflections from probe data, if our octahedral irradiance ever feeds
  specular.

[^post]: Matos, FF7 Rebirth GI
