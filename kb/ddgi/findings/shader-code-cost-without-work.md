---
type: Finding
title: Extra code in the apply pass costs even when it does no work
description: Probe marking inside the DDGI apply pass cost 0.05 ms at 1080p however few pixels ran it; a separate tiny pass cost 0.03 ms.
tags: [ddgi, performance, shaders, gpu]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 22
---

# What happened

Sample-driven scheduling needs to know which probes visible surfaces
sample. The first version marked them inside the apply pass, in the probe
loop of `ddgi_sample_volume`. RTX 3060, 1080p room, apply pass:

| Variant | Apply ms |
| --- | --- |
| No marking | 0.662-0.676 |
| Every pixel marks | 0.698-0.708 |
| One pixel per 2x2 or 4x4 block marks | 0.723-0.729 |
| A rotating quarter of the 8x8 workgroups marks | 0.711 |
| Flag set to a constant false (the compiler removes the code) | 0.662-0.676 |
| Separate pass, one thread per 8x8 tile (shipped) | 0.69-0.70 in total |

Fewer marking pixels did not help. A scattered subset is worse than all
pixels, because a warp with any marking thread waits for every thread.
The cost comes from the code being in the hot loop: registers, divergence
and the storage buffer binding. The number of stores matters much less.

# How to apply

- Don't add per-pixel side work to the apply pass, even behind a cheap
  runtime condition. Put it in a separate small pass that reads the same
  depth and normals at low density.
- Measure with the code compiled out (a constant false flag) against the
  real variant: the difference is the price of the code being there.
