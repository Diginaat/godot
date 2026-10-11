---
type: Success
title: Sample-driven probe scheduling
description: Updating the probes visible surfaces sample first made DDGI follow light changes about 12% faster and cut the motion error 8%, for 0.03 ms.
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
the scheduler gives them 3x the rate. RTX 3060, interior:

| | Before | After |
| --- | --- | --- |
| Frames to 90% after a light change | 230-231 | 202-207 |
| Outdoor motion error | 1.000 | 0.91-0.92 |
| Cost | | +0.03 ms |

Flicker stayed in range and the game level showed no regression. Commit
on `dev-ddgi-sched`; results in
[the proposal](/ddgi/improvements/sample-driven-updates.md).

# What made it work

- A measurable target from the paper.
- A small, separate pass.
- The whole test suite on each variant: it caught the flicker from a too
  short window.

[^yotei]: Ghost of Yotei ambient lighting
