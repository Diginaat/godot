---
type: Proposal
title: Compressed baked DDGI data
description: Smaller DDGIProbeData resources, from texture compression of the atlases up to learned compression.
tags: [ddgi, baking, compression, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: gpc
    resource: /research/gaussian-probe-compression.md
    title: Gaussian compression for probes
  - id: enemies
    resource: /research/enemies-adaptive-probe-volumes.md
    title: Enemies (L0-relative SH storage)
---

# Today

The interior test scene bakes 4968 probes into 3.6 MB (compressed
resource); 20736 probes into proportionally more. Atlases are RGBA half
(irradiance) and RG half (distance).

# Options, cheapest first

1. BC6H for the irradiance atlas, keep distance moments uncompressed (they
   need precision); check leaks and banding.
2. L0-relative storage (Enemies, after Frostbite) if we ever store SH.[^enemies]
3. Gaussian + MLP compression (about 1:50 on SH probes, minutes to
   compress, needs a decoder at load).[^gpc] Not justified at today's sizes.

Do nothing until a project hits a size problem.

[^gpc]: Gaussian compression
[^enemies]: Enemies
