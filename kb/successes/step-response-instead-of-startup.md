---
type: Success
title: Measuring the response to a change instead of startup convergence
description: Converge first, then change one thing and trace the GI buffer; repeats to a few frames where startup convergence varied threefold.
tags: [success, measuring, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What worked

Convergence from startup wasn't repeatable: the interior GI mean at frame
20 was 10, 16 or 30 in identical runs. Instead, the test converges for 400
frames, then makes one change and traces the GI buffer every frame:

- `--sun_step=0.5`: halves the sun. Frames to 90% repeated to within 10
  frames (202-207, 230-231).
- `--emissive_step=1`: an emissive sphere jumps 4.5 m across the room, with
  the noise floor subtracted. 50% / 90% repeated to within a few frames.

# Why it works

It measures only what changed, from a settled state, after the
asynchronous shader compiles are done.

# Reuse

Any "how fast does X follow" question: converge, step one input, trace.
See [the test protocol](/tooling/ddgi-test-protocol.md).
