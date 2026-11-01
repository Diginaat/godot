---
type: Reference
title: Cheap good-looking real-time glass
description: How games shade glass in one forward pass - thin-walled panes with two-interface Fresnel, screen-texture refraction from a blur mip chain (frosted glass), depth checks against foreground leaks, Beer-Lambert tint by thickness, parallel-slab refraction, cheap dispersion.
tags: [research, glass, transparency, refraction]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
sources:
  - id: doom
    title: "A. Courreges, DOOM (2016) - Graphics Study"
    url: https://www.adriancourreges.com/blog/2016/09/09/doom-2016-graphics-study/
  - id: doometernal
    title: "S. Coenen, DOOM Eternal - Graphics Study"
    url: https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy
  - id: cyberpunk
    title: "Sikachev et al., Transparency Rendering in Cyberpunk 2077 (SIGGRAPH 2021 talk)"
    url: https://history.siggraph.org/learning/transparency-rendering-in-cyberpunk-2077-by-sikachev-slega-nowakowski-and-kowalczyk
  - id: cyberpunkmat
    title: "Cyberpunk 2077 modding wiki, transparent material properties"
    url: https://wiki.redmodding.org/cyberpunk-2077-modding/for-mod-creators-theory/materials/configuring-materials/glass-material-properties
  - id: openpbr
    title: "OpenPBR: Novel Features and Implementation Details (thin-walled slab)"
    url: https://arxiv.org/pdf/2512.23696
  - id: lumaglass
    title: "luma.gl, Glass effects (backdrop blur mip chain)"
    url: https://luma.gl/docs/api-guide/shaders/glass-effects
  - id: godotrefraction
    title: "Godot proposals discussion 9580, Fixing refraction"
    url: https://github.com/godotengine/godot-proposals/discussions/9580
  - id: unityleak
    title: "Unity forum, HDRP distortion distorts foreground objects"
    url: https://discussions.unity.com/t/hdrp-transparent-material-with-distortion-causes-foreground-objects-to-be-distorted/723330
  - id: godotshaders
    title: "Godot Shaders, Colored Glass (Godot 4.2)"
    url: https://godotshaders.com/shader/godot-4-2-colored-glass/
---

# Summary

- **Frosted glass from a blur pyramid (Doom 2016, Doom Eternal).** Before
  the transparent pass, the opaque scene is copied into several blur
  levels; glass is drawn back to front in forward and each pixel reads the
  level its roughness asks for, so decals can make parts of a pane more or
  less frosted.[^doom][^doometernal] The same idea as a mip chain sampled
  with a roughness-driven LOD, made with wide smooth kernels so coarse
  levels don't look blocky.[^lumaglass] Godot's screen texture already has
  blurred mipmaps, and its shaders commonly use `roughness * 8` as the LOD.[^godotshaders]
- **Thin-walled vs solid.** A window is a thin sheet: no ray bending, the
  two interfaces and the absorption sum to a closed form (OpenPBR's thin
  slab), much cheaper than a solid mesh with volume.[^openpbr] Solid glass
  (bottles, blocks) refracts and tints with Beer-Lambert,
  T = exp(-sigma * distance).[^openpbr]
- **Cyberpunk 2077.** A distortion approach plus a *parallel slab*
  refraction approximation (the glass treated as a slab of a given depth,
  so the offset follows from IOR and depth without tracing), and per
  material IOR, refraction depth, a Fresnel bias and a blur
  radius.[^cyberpunk][^cyberpunkmat]
- **Foreground leaks.** Screen-space refraction samples whatever is on
  screen at the offset UV, including objects in front of the glass, which
  then appear warped inside it.[^unityleak] The fix is a depth test: if the
  depth at the offset UV is in front of the glass surface, use the
  unrefracted UV (or shrink the offset).[^godotrefraction]

# What it means here

- One forward pass, no blending: the glass writes
  `background(refracted UV, LOD from roughness) * transmittance + reflection * F`
  with alpha 1 in the transparent pass, so it isn't washed out by alpha
  blending. Only glass behind glass loses the far pane (it isn't in the
  screen texture), as in Doom.
- Panes: thin-walled. Fresnel from IOR 1.5 (F0 0.04) for both
  interfaces (about 2F / (1 + F)), tint once per pane, no refraction
  offset or a tiny one from the normal map.
- Solids: refraction offset by the refracted view vector times a
  thickness (per material, or from the object's bounds), Beer-Lambert tint
  over that thickness, depth-tested against foreground leaks; optional
  dispersion as three taps with slightly different IORs.
- Blur like the mirror pass: LOD from roughness and the distance between
  the glass and what is behind it (the depth buffer), so contact stays
  sharp and the far background blurs.
- Reflections come from what exists: probes (DDGI normalized), the
  planar mirror pass for flat panes, Fresnel weighted.

[^doom]: DOOM (2016) graphics study
[^doometernal]: DOOM Eternal graphics study
[^cyberpunk]: Transparency Rendering in Cyberpunk 2077
[^cyberpunkmat]: Cyberpunk 2077 material properties
[^openpbr]: OpenPBR implementation details
[^lumaglass]: luma.gl glass effects
[^godotrefraction]: Godot proposals 9580
[^unityleak]: Unity HDRP distortion thread
[^godotshaders]: Godot Shaders, Colored Glass
