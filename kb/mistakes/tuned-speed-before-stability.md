---
type: Mistake
title: Tuned a scheduler for speed without checking flicker
description: 3x the update rate for visible probes raised static flicker up to 0.61%; flicker was measured only after the speed tests.
tags: [mistake, ddgi, stability, flicker]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The sample-driven scheduler was tuned on response speed and motion error
(3x the rate for visible probes). The flicker test ran last and showed up
to 0.49% of pixels flickering. The real cause, the 3x rate, was found only
later with more runs; 2x shipped
([the finding](/ddgi/findings/scheduler-favored-set-stability.md)).

# Rule

Run the whole suite on every DDGI variant before deciding:

- speed (step response);
- motion error;
- flicker, 6 runs;
- weather fade;
- cost;
- the game level.

A production build doesn't trade stability for speed.
