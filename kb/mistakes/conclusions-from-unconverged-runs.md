---
type: Mistake
title: Reported a bug from runs that hadn't converged
description: Two false claims (SSAO changes DDGI, the baseline is unstable) came from comparing DDGI after only 120 frames.
tags: [mistake, ddgi, measuring]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

During the XeGTAO work, an agent compared DDGI brightness after 120 frames
and told the user that Godot's SSAO changed the DDGI result, and later that
the baseline itself was unstable. Both were wrong. At 120 frames the
budgeted scheduler hasn't converged, and its pace depends on unrelated
frame work. At 600 frames the same runs agreed within 2%.

# Why

The test was too short for what it measured, and one pair of runs was
taken as proof.

# Rule

- Before reporting a difference, repeat it and run past convergence (see
  [the convergence finding](/ddgi/findings/measure-after-convergence.md)).
- Tell the user "not confirmed yet" until it is.
