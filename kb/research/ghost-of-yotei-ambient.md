---
type: Reference
title: Ambient diffuse lighting in Ghost of Yotei
description: Sucker Punch's octree probes, interior masking, relit octahedral G-buffer probes and screen-space ray traced probes.
tags: [research, probes, rtgi, light-leaks, screen-space-probes]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://gdcvault.com/
    title: Wohllaib, Ambient Diffuse Lighting in Ghost of Yotei, GDC talk (Sucker Punch Productions)
---

# Summary

**Octree probes** (replace hand-made tetrahedral meshes): artist box,
octree subdivided where geometry is (inflated), probes at node corners,
neighbouring nodes at most one level apart so blending toward the parent
node near faces stays continuous; edge fade to a world grid. Probes inside
geometry are found at bake by back-facing normals in a double-sided
G-buffer capture, culled, and at runtime **replaced by a blend of their
neighbours** instead of being skipped. Normal-based weight on probes
behind the surface.[^talk]

**Interior masking**: every surface and probe is interior or exterior
(surfaces may blend at doors and windows); interior surfaces ignore
exterior probes. Authoring eased by watertight **interior volume meshes**
rendered like stencil shadows over the depth buffer. Fallbacks when no
matching probe: parent node, then a cached adjacent node with interior
probes, then raise tiny weights above epsilon. Dynamic objects: ray tests
to the 8 probes over frames. Works for thin walls (paper screens); only two
states.

**Relit probes**: each probe stores a baked octahedral mini G-buffer
(albedo, translucency, normal, distance, sky visibility, interior mask;
16x16 octree, 8x8 grid, BC7) and is relit at runtime, then converted to SH
with per-texel solid angle weights. Reacts to local lights; lighting a
texel is the same code as lighting an RT hit.

**Update selection**: mark every probe that pixels sample; next frame
relight the sampled ones older than one frame (12,000 probes in view down
to 1,200), spend leftover budget on older probes; relight all on camera
cut or load.

**RTGI** (30 Hz mode): screen-space probes (16x16 tiles, extra probes where
the plane/normal of existing ones covers a pixel poorly, up to 32k),
64 rays each guided by last frame's 8x8 irradiance octmap (prefix sum +
binary search), max 4 m then fall back to world probes; hits lit from a
32x32 per-mesh **material swatch**; 7x7 tile spatial filter with hit
distance reprojection; history rejection by luminance difference
normalized by the larger value, averaged per probe (from AMD GI-1.0);
per-pixel history. PS5 1440p: 6.1 ms (cast rays 3.3).

# For this fork

- [Sample-driven probe updates](/ddgi/improvements/sample-driven-updates.md)
- [Interior volumes against thin-wall leaks](/ddgi/improvements/interior-volumes.md)
- [Neighbour blend for inside probes](/ddgi/improvements/inside-probe-neighbour-blend.md)
- [Screen-space probes for near-field detail](/ddgi/improvements/screen-space-probes.md)
- Our guided rays (vMF lobe from a moving average) do the same job as their
  prefix-sum guiding.

[^talk]: Wohllaib, Ghost of Yotei, GDC
