---
type: Success
title: Sample-driven probe scheduling
description: Updating the probes visible surfaces sample first made DDGI follow light changes about 8% faster and cut the motion error 6%, at no measurable cost.
tags: [success, ddgi, scheduling]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
---

# What worked

The idea comes from a shipped game (Ghost of Yotei): relight the probes
that pixels use.[^yotei] Here, a mark pass stamps the sampled probes, and
the scheduler gives them 2x the rate. RTX 3060, interior:

| | Before | After |
| --- | --- | --- |
| Frames to 90% after a light change | 230-231 | 211-215 |
| Outdoor motion error | 1.000 | 0.937 |
| DDGI total, 1080p | 1.329 ms | 1.317-1.335 ms |

Flicker stayed in range and the game level showed no regression. Commit
on `dev-ddgi-sched`; results in
[the proposal](/ddgi/improvements/sample-driven-updates.md).

# What made it work

- A measurable target from the paper.
- A small, separate pass.
- The whole test suite on each variant, with enough runs: 6 flicker runs
  showed that 3x the rate added flicker and 2x did not.

[^yotei]: Ghost of Yotei ambient lighting
