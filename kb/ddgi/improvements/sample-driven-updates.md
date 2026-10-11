---
type: Proposal
title: Sample-driven probe updates
description: Let the DDGI apply pass mark the probes it samples, and give marked probes priority in the next frame's schedule.
tags: [ddgi, scheduling, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
---

# Today

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
