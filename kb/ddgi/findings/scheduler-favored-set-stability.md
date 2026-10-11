---
type: Finding
title: A changing set of favored probes shows as flicker
description: When the scheduler's favored probe set changed every few frames, static interior flicker rose; a 16 frame window keeps the set stable.
tags: [ddgi, scheduling, flicker, stability]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 22
---

# What happened

The mark pass samples one pixel per 8x8 tile per frame, at a rotating
position. With a 4 frame "recently sampled" window, which probes counted
as sampled changed from frame to frame. Their update rate changed with
it. Static interior flicker (600 + 60 frames) rose to 0.07% and 0.49% of
pixels. The baseline was 0.01-0.06% over 3 runs.

With a 16 frame window: 0.03-0.06%, std 0.163-0.191 (baseline
0.156-0.172). The remaining small increase is expected: visible probes
update more often, so their update noise shows a little more.

# How to apply

- Priority inputs to the scheduler must be stable over time. Use a window
  several times longer than the sampling period.
- Measure flicker 3 times per variant. One run moved between 0.01% and
  0.06% on an unchanged build.
