---
type: Finding
title: Favoring visible probes too much adds flicker
description: Giving probes that visible surfaces sample 3x the update rate added flicker in a static scene; 2x keeps most of the speed without it.
tags: [ddgi, scheduling, flicker, stability]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 22
---

# What happened

Static interior, 600 + 60 frames, 6 runs per build:

| Variant | flicker_std | Pixels over 2 levels | Frames to 90% after a sun change |
| --- | --- | --- | --- |
| Before (in view 2x) | 0.155-0.177 | 0.01-0.19% | 230-231 |
| Sampled 3x, 16 frame window | 0.179-0.203 | 0.00-0.61% | 202-213 |
| Sampled 2x, 16 frame window (shipped) | 0.155-0.177 | 0.00-0.11% | 211-215 |

Every probe update adds its noise to the stored value. Updating the
visible probes more often makes them follow changes faster, but also adds
more noise per frame.

An earlier version of this finding said that a 16 frame "recently
sampled" window (instead of 4) removed the flicker. That came from 3 runs
and was wrong: see
[the mistake](/mistakes/three-runs-of-a-noisy-test.md).

# How to apply

- Treat the update rate of visible probes as a noise budget, not only a
  speed knob. Above about 2x the base, the flicker rises.
- Faster response for a probe whose light really changes comes from the
  change detection in the blend (lower hysteresis on a confirmed change),
  not from updating it more often in steady state.
