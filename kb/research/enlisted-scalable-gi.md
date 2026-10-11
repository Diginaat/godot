---
type: Reference
title: Scalable real-time global illumination for large scenes (Enlisted)
description: Gaijin's voxel scene clipmap fed back from the G-buffer, irradiance clipmap of ambient cubes, probe update queues, interior convexes.
tags: [research, gi, clipmap, light-leaks]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://gdcvault.com/
    title: Yudintsev, Scalable Real-time Global Illumination for Large Scenes, talk (Gaijin Entertainment)
---

# Summary

- **Irradiance clipmap**: 3 cascades of about 64x32x64, cell 0.45 m x 3^i,
  HL2 ambient cube basis; scene clipmap 128x64x128 at 0.25 m x 3^i holding
  lit color or albedo.[^talk]
- **Scene feedback**: 32k random G-buffer pixels per frame are relit and
  blended into the voxel scene, giving multiple bounces and fixing
  voxelization errors (thin walls).
- **Probe updates**: visible probes chosen stochastically by visibility and
  how much they changed; 1024-2048 rays each. **Separate queues**:
  first-seen probes asap at lower quality (256 rays, 4096 probes),
  non-intersecting probes rarely (1024 rays, 64-128).
- **Scrolling**: new finer-cascade texels are initialized by **copying the
  coarser cascade**, then marked "not really computed" so they are relit
  when visible. Camera teleport: two passes (direct only, then irradiance).
- **Leaks**: trilinear filtering leaks between indoor and outdoor; artist
  **convex volumes for interiors** filter sample positions; "window"
  convexes keep small openings from closing.
- **Reflections**: scale cubemap reflections by diffuse GI luminance
  (Treyarch), as our DDGI already does with its sky occlusion ratio.
- Cost on Xbox One: about 1.6 ms (0.7 ms async or one bounce).
- With hardware RT: trace real geometry for hit positions, store only
  albedo in the scene.

# For this fork

- [Initialize scrolled probes from the coarser cascade](/ddgi/improvements/scroll-init-from-coarser.md)
- Update queues by probe state match our scheduler's priorities; first-seen
  probes at lower ray count is a variant to test.
- [Interior volumes](/ddgi/improvements/interior-volumes.md)

[^talk]: Yudintsev, Enlisted GI talk
