---
type: Mistake
title: Tuned a scheduler for speed without checking flicker
description: A 4 frame "recently sampled" window raised static flicker up to 0.49%; flicker was measured only after the speed tests.
tags: [mistake, ddgi, stability, flicker]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The sample-driven scheduler was tuned on response speed and motion error.
The flicker test ran last. With a 4 frame window, 0.07% and 0.49% of
pixels flickered, against a 0.01-0.06% baseline. A 16 frame window fixed
it ([the finding](/ddgi/findings/scheduler-favored-set-stability.md)).

# Rule

Run the whole suite on every DDGI variant before deciding:

- speed (step response);
- motion error;
- flicker, 3 runs;
- weather fade;
- cost;
- the game level.

A production build doesn't trade stability for speed.
