---
type: Reference
title: Precomputed lighting in Call of Duty - Infinite Warfare
description: Activision's lightmaps (AHD) plus probes - decoupled self-visibility cones, k-means sampling points per object, a regular tetrahedral light grid with per-probe visibility, SH de-ringing, least-squares baking with regularization.
tags: [research, gi, probes, baking, light-leaks, spherical-harmonics]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://www.activision.com/cdn/research/Precomputed_Lighting_in_CoD_IW.pptx
    title: Iwanicki and Sloan, Precomputed Lighting in Call of Duty - Infinite Warfare (Activision Central Technology, 2017), slides with speaker notes
---

# Summary

Indirect light is lightmaps on big structural geometry and probes for
everything else; the game runs at 60 fps.[^talk]

- **Lightmaps, AHD**: ambient color + highlight color + highlight direction
  per texel, direction in tangent space (hemi-octahedral), colors in BC6H.
  Chosen over SH2, H-basis and eigen bases as the best value: cheap and
  high contrast with normal maps. It struggles with color bleeding from
  different directions and with fast direction changes.
- **Decoupled visibility**: the rendering equation splits into smooth
  incoming light and fast-changing visibility. Self-visibility is baked per
  mesh vertex as a cone (axis, angle, scale 0-1), shared between instances;
  baked from 4th order SH, then smoothed and inpainted. Shorter bake rays
  for enterable interiors and for skinned meshes.
- **Lighting per object**: several sampling points per object, placed by
  k-means over the surface (count from object size). Their covariance picks
  the interpolation: 1 point, a line (1D), a plane (triangulated) or a
  cloud (tetrahedralized). Each vertex stores 2 x 4 bytes of indices and
  weights. Lighting x visibility is a mixed-order SH product in the vertex
  shader, converted to AHD for the pixel shader.
- **Light grid**: about 100,000 probes per level, about 60 MB (much of it
  visibility), 0.2-0.4 ms of async lookups for tens of thousands of
  queries per frame (not per pixel), with slight temporal filtering
  against flicker on fast objects.
  - Random points + Delaunay gave long, thin tetrahedra and temporal
    artifacts. Instead: voxelize the artist volumes coarsely,
    tetrahedralize the boxes (regular tets), subdivide (tet into 4 tets +
    an octahedron) near geometry, the navmesh and marked regions, with
    neighbors at most one level apart. Then coarsen where the lighting
    error between levels is small.
  - **Leaks**: each probe stores, per tetrahedron it touches, a triangular
    depth map of 15 values x 8 bits (4 DWORDs) toward the opposite face, so
    a probe that doesn't see the lookup point is masked.
- **SH de-ringing**: find the most negative value of the SH (rotate to the
  optimal linear direction, analytic minimum of the zonal part, a bound for
  the rest) and binary-search the weakest window that keeps the
  cosine-convolved light non-negative. One window for all three color
  channels (the strictest), so ringing can't make colored fringes.
- **Baking by least squares**: solve for probe values such that the
  *interpolated* result (with visibility) matches many samples inside each
  tetrahedron. A Laplacian regularizer penalizes differences between
  probes that see each other. This also denoises: individual samples can
  be cheaper. Same for static objects (non-negative DC, a bounded ratio of
  linear to DC energy).
- Conclusion: separate the terms of the rendering equation, resample
  detailed data into simpler structures, and always regularize least
  squares.

# For this fork

- [Least-squares baking](/ddgi/improvements/least-squares-bake.md): our
  bake stores each probe's own traced light. Solving for the values that
  make the *interpolated* light match samples, with a smoothness term
  between mutually visible probes, would remove interpolation errors and
  noise from baked data.
- Their per-probe visibility is the same idea as DDGI's distance octahedra
  (Chebyshev test); ours is per direction, theirs per tetrahedron face.
- Temporal filtering of looked-up light for fast-moving objects: our
  display smoothing does the same job per probe.
- Regular cells beat random points for interpolation stability: keep that
  in mind for [adaptive density](/ddgi/improvements/adaptive-density.md).

[^talk]: Iwanicki and Sloan, CoD: IW precomputed lighting
