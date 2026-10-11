---
type: Proposal
title: Contact GI on XeGTAO
description: During the XeGTAO horizon search, also gather the light at the occluding points, so contacts get bounce light instead of only darkening.
tags: [ddgi, xegtao, ambient-occlusion, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: sdfddgi
    resource: /research/sdfddgi.md
    title: SDFDDGI (Contact GI)
---

# Idea

SDFDDGI's Contact GI: while computing GTAO, sample the lighting at the
occluded (horizon) points and merge it with the probe GI, so occluded
detail receives bounce light instead of plain darkening; 0.41 ms at 1080p
on an RTX 2080 Ti.[^sdfddgi] AO alone darkens corners that DDGI may
already account for.

# Proposal (after XeGTAO is merged)

1. In the XeGTAO main pass, at the sample that sets each horizon, read last
   frame's lit color (or this frame's diffuse lighting buffer) and albedo.
2. Accumulate a cosine-weighted occluded radiance per pixel; output it
   with the visibility.
3. In the scene shader: ambient = probe GI x visibility + occluded
   radiance x albedo (the occluded part replaces the probe light that AO
   removed, not adds to it).

# Risks

- Screen-space only; last-frame color needs reprojection and is noisy.
- Must not count light twice where DDGI already has the bounce.

[^sdfddgi]: SDFDDGI
