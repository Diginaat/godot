---
type: Mistake
title: Recorded a long run of a scene that couldn't show the problem
description: A 24-second recording of an existing scene (far camera, spinning sun, tiny sphere, no ground) said nothing about blobs in the indirect light; the user called it useless.
tags: [mistake, testing, visuals]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

Asked to run a scene longer, an agent recorded 1430 frames of it as it
was. The camera was far away, the sun rotated through the ground half of
the time, the moving sphere covered a few pixels, and there was no
surface next to it to receive its bounce light. The recording took ten
minutes and showed nothing about the problem being fixed (blotchy,
lagging indirect light).

# Rule

- Before a long run, check one frame: does the scene show the effect
  (the object, the surface that receives its light, the light source)
  big enough to judge?
- For indirect light from a moving object, use a close view of the object
  next to a surface (the test project's `bleed` view), in the GI buffer
  view, as an animated contact sheet.
