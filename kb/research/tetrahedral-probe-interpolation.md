---
type: Reference
title: Light probe interpolation using tetrahedral tessellations (GDC 2012)
description: Unity's light probes - Delaunay tetrahedralization of free-placed probes, barycentric interpolation, open cells outside the hull, coherent tetrahedron walks.
tags: [research, probes, interpolation, adaptive-density]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://gdcvault.com/play/1015312/Light-Probe-Interpolation-Using-Tetrahedral
    title: Cupisz, Light Probe Interpolation Using Tetrahedral Tessellations, GDC 2012 (Unity Technologies)
---

# Summary

- Probes are free-placed points. A Delaunay tetrahedralization connects
  them; a point is lit from the 4 probes of its tetrahedron with
  barycentric weights. C0 continuous, little memory.[^talk]
- Outside the probe hull, synthetic "open cells" extend the tessellation.
- Each object keeps its last tetrahedron and walks to the neighbor in the
  direction of the most negative barycentric coordinate, so lookups are
  temporally coherent: 2000 queries in under 1 ms on an iPad 2.
- Per object, not per pixel (the abstract gives no SH order).

# For this fork

- Our probes are on a regular grid with trilinear weights, so this isn't
  needed today. It matters if probes become irregular
  ([adaptive density](/ddgi/improvements/adaptive-density.md)): Call of
  Duty found that Delaunay meshes of random points give long, thin
  tetrahedra that flicker, and built regular ones instead
  ([CoD: Infinite Warfare](/research/cod-iw-precomputed-lighting.md)).
- The coherent walk is a cheap way to look up irregular structures per
  object (VFX, volumetrics, CPU queries).

[^talk]: Cupisz, GDC 2012
