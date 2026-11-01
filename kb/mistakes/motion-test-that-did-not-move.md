---
type: Mistake
title: A walking test that didn't walk
description: The showcase's --move flag set the player's position each frame, but the PhysX character controller pulled it back, so "stable while moving" checks were taken standing still.
tags: [mistake, testing, reflections]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
---

# What happened

- The reflection showcase's `--move=dx,dz,dyaw` moved the player node in
  `_process`. The PhysX character controller keeps its own position and
  snapped the body back to it, with a visible jump every 27 frames. Only
  the turning took effect.
- Planar reflections were declared stable while moving from these movies.
  The user then saw instability and other bugs while playing.
- Found when a walk away from the mirror never got farther from it (the
  mirror kept its size in frames 150 to 470).

# Rule

- Check that a motion test moves: put a frame from the start and one from
  the end side by side before reading anything into the movie.
- Drive a test camera without the physics body (physics processing and
  interpolation off), or through the controller's own input.
- Look for periodic spikes in the frame-to-frame difference: a jump at a
  fixed period is the harness, not the renderer.
