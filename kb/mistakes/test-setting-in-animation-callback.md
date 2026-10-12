---
type: Mistake
title: A test setting lived in the animation callback
description: The bleed view's sun was set in a per-frame callback that only runs while animating, so every still test (--anim=0) used the default sun.
tags: [mistake, testing, test-project]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The test project's sun is turned by an animation callback (time of day).
The bleed view's own sun direction and energy were added to that
callback. Still tests run with `--anim=0`, which skips the callbacks, so
four rounds of still shots (and a path-traced reference) showed the
default sun while the animated recordings showed the intended one. The
results looked contradictory ("no red on the wall") until the wrong
shadow direction in a final image gave it away.

# Rule

- Put a view's fixed setup in the view setup (`_set_view()`), never in
  code that runs only while animating.
- Check one final (not debug) image of a new test scene before measuring
  with it: are the lights and shadows where the scene meant them?
