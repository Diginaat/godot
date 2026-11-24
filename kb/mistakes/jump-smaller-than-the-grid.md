---
type: Mistake
title: Ruled out scrolling with a jump smaller than the probe grid
description: A teleport shorter than the DDGI grid scrolls the volume instead of resetting it, so it can't rule out a scroll bug.
tags: [mistake, ddgi, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What happened

To test whether a DDGI artifact came from scrolling, the player was
teleported to the spot instead of walked there. The artifact stayed, so
scrolling was ruled out, and time went to floor offsets, hiding geometry
groups and a transform cache. The jump was 14 m and 5 m on a 28 x 29
probe grid: less than the grid, so `_setup_volumes()` scrolled the
volume, the same code path as walking. The bug was in scrolling.

# Rule

To rule out scrolling, move farther than the grid on some axis (a full
reset) or start the scene at the spot. Check which path the test takes
(`reset[3]` versus the scroll deltas) before drawing a conclusion.
