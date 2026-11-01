---
type: Reference
title: Cubemap normalization by diffuse irradiance
description: Scale a static reflection probe by the irradiance at the shaded point over the irradiance at the probe - Black Ops II, Frostbite, AC4, Unity 6 APV.
tags: [research, reflections, probes, normalization]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
sources:
  - id: cod
    title: "D. Lazarov, Getting More Physical in Call of Duty: Black Ops II, SIGGRAPH 2013 course"
    url: https://blog.selfshadow.com/publications/s2013-shading-course/lazarov/s2013_pbs_black_ops_2_notes.pdf
  - id: frostbite
    title: "S. Lagarde, C. de Rousiers, Moving Frostbite to Physically Based Rendering 3.0, SIGGRAPH 2014 course"
    url: https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf
  - id: ac4
    title: "B. Wronski, Assassin's Creed 4: Black Flag - Road to next-gen graphics, GDC 2014"
    url: https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf
  - id: unity
    title: Unity ProbeVolumeShadingParameters (reflection probe normalization clamps)
    url: https://docs.unity.cn/Packages/com.unity.render-pipelines.core@15.0/api/UnityEngine.Rendering.ProbeVolumeShadingParameters.html
---

# Summary

- Black Ops II: the environment map is divided by its own irradiance and
  multiplied by the lightmap's at the pixel; the improved version
  evaluates the map's SH at the vertex normal.[^cod]
- Frostbite: store cubemap / diffuse probe at the cubemap's position;
  shade with cubemap_norm x diffuse probe at the shaded position.[^frostbite]
- Assassin's Creed 4: deferred normalized irradiance probes for time of
  day.[^ac4]
- Unity 6 Adaptive Probe Volumes normalize reflection probes by the APV
  irradiance, with lower and upper clamps.[^unity]

# For this fork

DDGI gives the irradiance at every pixel (the GI buffer). The probe's own
irradiance is its roughest octahedral level in the normal direction. The
factor uses luminance (no color shift), is clamped to [0, max] and blended
by DDGI's coverage of the pixel; settings
`probe_ddgi_normalization` and `probe_normalization_max`. It also closes
the leak where probes replaced the DDGI-occluded sky (see the specular
occlusion proposal).

[^cod]: Lazarov 2013
[^frostbite]: Lagarde and de Rousiers 2014
[^ac4]: Wronski 2014
[^unity]: Unity APV
