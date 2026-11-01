---
type: Reference
title: Mirrored-camera planar reflections (engines and tutorials)
description: How engines render real mirrors - a second scene render from the camera reflected across the plane, clipped at the plane, flipped culling, reduced resolution, roughness prefiltering with contact hardening; read for step 8.
tags: [research, reflections, planar, mirror]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
sources:
  - id: lengyel
    title: "E. Lengyel, Oblique View Frustum Depth Projection and Clipping"
    url: https://terathon.com/lengyel/Lengyel-Oblique.pdf
  - id: arasortho
    title: "A. Pranckevicius, Oblique near-plane clipping with orthographic camera"
    url: https://aras-p.info/texts/obliqueortho.html
  - id: vulkan
    title: "Vulkan tutorial, Planar Reflections in Our Engine"
    url: https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Advanced_Topics/Planar_Reflections.html
  - id: unreal
    title: "Unreal Engine documentation, Planar Reflections"
    url: https://dev.epicgames.com/documentation/en-us/unreal-engine/planar-reflections-in-unreal-engine
  - id: hdrp
    title: "Unity HDRP documentation, Planar Reflection Probe"
    url: https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition%4011.0/manual/Planar-Reflection-Probe.html
  - id: hdrpfilter
    title: "Unity Graphics PR 337 (planar reflection filtering)"
    url: https://github.com/Unity-Technologies/Graphics/pull/337/files
---

# Summary

- **The render.** The camera is reflected across the plane and the scene
  is rendered again from there into a texture; reflective surfaces sample
  it at the screen position of their pixel. It sees what is off screen and
  behind the camera, with the scene's own lighting and shadows.[^vulkan]
- **Clipping at the plane.** Geometry between the mirrored camera and the
  plane (the wall the mirror hangs on) must not be drawn. The usual tool
  is an oblique projection: the near plane is moved onto the mirror plane,
  so no extra clip plane or shader variant is needed; it costs depth
  precision far away and breaks anything that reads near/far from the
  projection.[^lengyel] Orthographic cameras need a variant.[^arasortho]
  Hardware clip distances are the alternative when the projection must
  stay standard.
- **Winding.** A reflection flips triangle winding: flip the cull mode (or
  disable culling) for the mirrored pass.[^vulkan]
- **Cost controls (Unreal).** A screen percentage for the reflection
  render, a prefilter roughness (blur that also hides the low
  resolution) reached at a prefilter distance, normal distortion from the
  reflecting mesh's normals for waves. Unreal says it costs a full scene
  render and is more accurate than SSR.[^unreal]
- **Rough reflections (HDRP).** HDRP can use a planar probe as a pure
  mirror (cheaper) or filter it for rough surfaces: a mip chain whose
  level comes from the pixel's footprint, estimated from the roughness and
  the distance between the surface and what it reflects, so contacts stay
  sharp and far things blur (contact hardening). The 2D texture is
  convolved with a Gaussian, not GGX, so roughness maps to mips
  differently than for cubemaps.[^hdrp][^hdrpfilter]

# What it means here

- Clip with `gl_ClipDistance` from the scene vertex shaders rather than an
  oblique projection: Godot derives cluster slices, fog, LOD and shadow
  splits from the camera projection's near and far, which an oblique
  matrix breaks. Vulkan enables `shaderClipDistance` already.
- Forward+ already reverses culling when the camera basis has a negative
  determinant (`render_forward_clustered.cpp`, `reverse_cull`), so a
  reflected camera transform renders with correct winding.
- Composite exactly like the PPR result (replaces the incoming specular
  light before the split-sum BRDF), so Fresnel and roughness stay
  physical; blur by roughness times the reflected depth for contact
  hardening.

[^lengyel]: E. Lengyel, Oblique View Frustum Depth Projection and Clipping
[^arasortho]: Oblique near-plane clipping with orthographic camera
[^vulkan]: Vulkan tutorial, Planar Reflections in Our Engine
[^unreal]: Unreal Engine, Planar Reflections
[^hdrp]: Unity HDRP, Planar Reflection Probe
[^hdrpfilter]: Unity Graphics PR 337
