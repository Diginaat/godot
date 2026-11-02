---
type: Mistake
title: A flicker metric that could not see the flicker
description: Twice an automatic measure said a mirror flicker was absent or fixed while the frames still showed it; the metric counted the wrong pixels.
tags: [mistake, testing, reflections, measuring]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-02T00:00:00Z }
---

# What happened

- Sky flashes in a mirror (single white frames) were measured by counting
  "sky-blue" pixels. A first test strafed sideways (the flicker needs
  walking toward the mirror) and found nothing; a later fix attempt
  "removed" the drops in blue pixels, but the crops still showed white
  flashes: the white sky still passed the blue test after a threshold
  shift.
- The cause was only found after correlating the flash frames with an
  engine event (a temporary print of the mirror buffers' resize frames).

# Rule

- Look at zoomed crops of the frames around each detected (or expected)
  event before trusting a metric, every time, also after a fix.
- Measure the artifact itself (here: near-white pixels or one-frame jumps
  against both neighbors), not a proxy.
- Reproduce the user's exact motion (toward, not past, the object) and
  scene before measuring.
- Log engine events per frame (temporary prints) and line them up with
  the bad frames; remove the prints before committing.
