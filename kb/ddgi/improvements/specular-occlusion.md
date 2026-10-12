---
type: Proposal
title: Specular occlusion for the DDGI reflection output
description: Darken sky and reflection-probe reflections where DDGI, the direct shadow and XeGTAO bent normals say the light can't reach, so interiors don't show bright outdoor reflections.
tags: [ddgi, reflections, visuals, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ff7
    resource: /research/ff7-rebirth-gi.md
    title: FF7 Rebirth GI analysis
  - id: enlisted
    resource: /research/enlisted-scalable-gi.md
    title: Enlisted scalable GI
---

# Today

The DDGI apply pass writes a reflection buffer. It already scales sky and
reflection probe light down in dark areas, from the ratio of DDGI light
to the open-sky light (step "occlude sky and reflection probe light in
dark areas"). It doesn't use the direct shadow, and XeGTAO's bent normals
aren't used for specular.

# Proposal

FF7 Rebirth's reflections leaked into shadows and interiors; the fixes
were to suppress cubemap reflections where the direct light is shadowed,
and bent-normal specular occlusion.[^ff7] Enlisted scales cubemaps by the
diffuse GI luminance, as our current fix does.[^enlisted]

1. Specular occlusion from the XeGTAO visibility cone (bent normal and
   cone angle) against the reflection lobe (cone-cone intersection, as in
   "GTSO").
2. Optionally weigh in the sun shadow at the pixel: a surface in sun shadow
   indoors shouldn't reflect a bright sky.
3. Keep the DDGI ratio as the large-scale term; the two above add the
   small-scale detail.

# Measure

Interior and cave views: reflection brightness in shadowed corners
against the path tracer (reference); cost; no darkening of open outdoor
surfaces.

[^ff7]: FF7 Rebirth GI analysis
[^enlisted]: Enlisted scalable GI
