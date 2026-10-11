---
type: Finding
title: Compare DDGI only after convergence
description: At 120 frames the budgeted scheduler hasn't converged and its pace depends on unrelated frame work; compare at 600+ frames.
tags: [ddgi, measuring, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# Finding

Two runs of the interior test view that differed only in unrelated frame
work (Godot SSAO on or off, a sync point) gave DDGI GI buffer means that
differed by up to 20% at 120 frames, but by under 2% at 600 and 1200
frames. Which probes win the limited update slots each frame depends on GPU
thread order, so the budgeted scheduler converges at a pace that changes
with everything else in the frame. Realtime updates (`--realtime=1`) were
identical in all runs.

# How to apply

- Compare DDGI brightness after 600+ frames (`--frames=600`), or with
  `--realtime=1` when the scheduler isn't what's being tested.
- Measure the GI buffer (`--debug=1`) to see DDGI alone. The probe sphere
  view (`--debug=2`) includes the raster image behind the spheres, which
  other effects (AO) change.

This cost a false bug report during the XeGTAO work; see
[the ordering finding](/ddgi/findings/render-graph-bounce-loss.md) for the
real issue found once runs were compared properly.
