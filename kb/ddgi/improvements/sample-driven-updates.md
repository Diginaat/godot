---
type: Proposal
title: Sample-driven probe updates
description: Let the DDGI apply pass mark the probes it samples, and give marked probes priority in the next frame's schedule.
tags: [ddgi, scheduling, proposal]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
---

# Result (2026-10-12, branch `dev-ddgi-sched`)

Implemented as a separate mark pass after the apply pass: one thread per
8x8 tile marks the 8 probes around one pixel of the tile (rotating every
frame) in a per-probe frame stamp buffer. Probes marked in the last 16
frames get 2x the rate. Probes in the frustum that no surface samples get
0.8x, off screen 0.6x. RTX 3060:

| | Before | After |
| --- | --- | --- |
| Interior, sun halved: frames to 90% / 98% | 230-231 / 272-273 | 211-215 / 264-267 |
| Overshoot | 2.2-2.7% | 1.6-2.0% |
| Outdoor motion error | 1.000 | 0.937 |
| DDGI total, 1080p room | 1.329 ms | 1.317-1.335 ms |
| Static flicker, 6 runs: std / pixels | 0.155-0.177 / 0.01-0.19% | 0.155-0.177 / 0.00-0.11% |
| Game level, storm, moving emissive: GI mean while moving, 3 runs | 17.9-21.0 | 20.2-20.8 |

Lessons: [marking in the apply pass was too expensive](/ddgi/findings/shader-code-cost-without-work.md),
[the favored set must be stable](/ddgi/findings/scheduler-favored-set-stability.md).
How it was measured: [DDGI test protocol](/tooling/ddgi-test-protocol.md).

# Before

The scheduler (`ddgi_update.glsl`, `MODE_SCHEDULE`) gives probes "in view"
twice the rate, tested by projecting the probe position into a frustum
widened by 20%. That counts probes behind walls and far cascades that no
pixel reads, and misses probes just outside the frustum that pixels near
the edge do read.

# Proposal

Ghost of Yotei marks every probe that pixels sample and relights the
sampled ones first; in their worst case that cut 12,000 probes in view to
1,200 that mattered.[^yotei]

1. A per-probe "sampled" frame stamp (in `DDGIProbe` or a separate uint
   buffer). The apply pass writes the current frame to the 8 probes of the
   cascade it actually uses (only weights above a threshold), with plain
   stores (same value from every writer, so no atomics needed).
2. The scheduler replaces the frustum test with: sampled last frame ⇒ x2
   (or x3), not sampled ⇒ x0.6 as now.
3. Keep the frustum test as a fallback for frames without an apply pass
   (bake viewport).

# Measure

Convergence frames in the interior and outdoor (`--move=1`) views, the
weather test lag, and flicker, before and after; apply pass cost (+8
stores per pixel at half resolution is cheap, at full resolution check).

[^yotei]: Ghost of Yotei ambient lighting
