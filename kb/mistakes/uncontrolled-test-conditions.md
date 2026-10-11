---
type: Mistake
title: Ran a GI test with auto exposure and real-time timing
description: The first game-level runs had auto exposure on and an uncapped frame rate, so exposure and time-based effects changed the results.
tags: [mistake, testing, exposure]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The first game-level DDGI runs (storm weather, moving emissive sphere)
kept the level's auto exposure and ran at an uncapped frame rate.

- Auto exposure adapts over seconds and rescales the whole image. That
  hides how fast the indirect light follows. The user had to point it
  out.
- Weather, light transitions (`light_transition_time` is in seconds) and
  the moving object advanced by different amounts per frame between runs.

# Rule

Before measuring, list everything in the scene that adapts or animates
over time and pin it:

- Exposure: fixed.
- Frame timing: `--fixed-fps 60`.
- Animation: frame-counted, not time-based.
- Weather: no random events (no lightning).
