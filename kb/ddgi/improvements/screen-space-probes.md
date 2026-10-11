---
type: Proposal
title: Screen-space probes for near-field detail
description: Ray traced probes spawned on visible surfaces add GI detail below the world probe spacing, falling back to DDGI beyond a short distance.
tags: [ddgi, rtgi, screen-space-probes, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
---

# Idea

Ghost of Yotei's RTGI: probes on screen surfaces (16x16 tiles, more where
geometry varies), 64 guided rays each up to 4 m, misses and far light from
the world probes, spatial filter with hit-distance reprojection, history
rejection, SH per probe, per-pixel blend. 6.1 ms on a base PS5 at
1440p.[^yotei]

# Fit for this fork

- We have the ray tracing pipeline, guided rays, and DDGI as the far-field
  fallback, so the pieces exist.
- It overlaps with the path tracer and the native denoiser work; a large
  feature, best as a quality tier above DDGI ("DDGI + near field").
- Do after the cheaper items; measure against the path tracer as
  reference.

[^yotei]: Ghost of Yotei
