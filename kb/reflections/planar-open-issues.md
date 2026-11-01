---
type: Proposal
title: Planar reflection open issues
description: Where the planar reflections were paused - blocky grazing mirrors, hidden undersides of small objects, cost growth - and the fixes to try next.
tags: [reflections, planar, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
sources:
  - id: runbook
    resource: ../REFLECTIONS.md
    title: REFLECTIONS.md, "Open issues"
---

# State

Pixel-projected planar reflections with footprint fill, coverage blending,
gap bridging, a normal check against contact bands and a temporal filter.
Stable while moving in the showcase level and the reflect test view.
The table of open issues, their repro positions and test commands is in
the runbook.[^runbook]

# Next, in order of value

1. Cost: ping-pong history, temporal pass only on reflector tiles, half
   resolution by default on weak GPUs (0.83 ms at 1080p now).
2. Mirrors seen from close by at grazing angles: bilinear footprint fill, or
   an opt-in mirrored-camera mode for mirrors (step 8).
3. Hidden undersides of small objects: needs a re-render or ray tracing;
   screen space can only fall back to the probe.
4. Per-mesh opt-in/opt-out for the automatic scan.

# Lessons

Test planar reflections with movement and look at consecutive frames:
still frames hid the stripes, the dotted grid in the mirror and the
shimmer. Each fix was checked against the reflect view for regressions
(a one-sided hole dilation made cones and ghosts there).

[^runbook]: REFLECTIONS.md, "Open issues"
