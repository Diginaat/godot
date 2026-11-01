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
Stable in the reflect test view; the showcase walks before 2026-11-01
evening didn't move (see the mistakes entry), so recheck in motion.
The table of open issues, their repro positions and test commands is in
the runbook.[^runbook]

# Next, in order of value

1. Cost: ping-pong history, temporal pass only on reflector tiles, half
   resolution by default on weak GPUs (0.83 ms at 1080p now).
2. Mirrors seen from close by at grazing angles: bilinear footprint fill, or
   the mirror pass (step 8, done 2026-11-01) now fills what the projection
   misses on the reflector covering most of the view, at about 0.35 to
   0.55 ms (1080p, cropped to the mirror's rectangle, no post-processing).
   Shared shadow maps would cut more.
3. Hidden undersides of small objects: done where the mirror pass runs
   (floors included by default); other reflectors still fall back.
4. Per-mesh opt-in/opt-out for the automatic scan: done (metadata
   `auto_planar_reflection`).
5. A mirror without the mirror pass shows its probe, captured without
   DDGI (cold, dark): DDGI in probe captures (step 9), or a second pass.
6. The showcase's indirect light is 3 to 7 times the path tracer's, which
   washes out the bright marble floor and its reflections (a DDGI
   question).

# Lessons

Test planar reflections with movement and look at consecutive frames:
still frames hid the stripes, the dotted grid in the mirror and the
shimmer. Each fix was checked against the reflect view for regressions
(a one-sided hole dilation made cones and ghosts there).

[^runbook]: REFLECTIONS.md, "Open issues"
