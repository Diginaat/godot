---
type: Reference
title: Adaptative placement of probes for global illumination (Forza Motorsport)
description: Turn 10's procedural, layered probe point clouds for baked GI, generated offline in Houdini PDG.
tags: [research, probes, baking, placement]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: talk
    resource: https://gdcvault.com/
    title: Garzon, Adaptative Placement of Probes for Global Illumination, talk (Turn 10 Studios)
---

# Summary

Baked probes for race tracks, placed procedurally as layered point clouds:
`((terrain + road) - intersecting geometry) + local fine envelope`.[^talk]

- **Global coarse envelope**: tiles over the track bounds, active only
  where traced geometry exists; density per tile from the normalized
  distance to the road, remapped to user-set probes per metre (4 to 128).
- **Local fine envelope**: sparse voxel volume (VDB) around hero geometry,
  dual-envelope activation, probes on active voxels.
- Remove probes intersecting geometry; merge layers.
- Generated on a farm (Houdini PDG). 125k probes bake in about 1 hour,
  38 MB compressed.
- Limit: the runtime data is still a low-resolution texture, so adaptive
  placement is capped by it.

# For this fork

Mostly a baking-pipeline idea: density where the player looks or drives.
For DDGIVolume it suggests **density from distance to important geometry**
for baked volumes; see
[adaptive density](/ddgi/improvements/adaptive-density.md). The slides
carry little text (mostly images), so details are limited.

[^talk]: Garzon, Forza probe placement talk
