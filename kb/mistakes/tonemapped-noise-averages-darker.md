---
type: Mistake
title: Averaged noisy frames after the tonemapper
description: Path tracer references averaged as tonemapped 8-bit frames came out up to 3 times too dark, and an auto exposure made settings look like they added light.
tags: [mistake, testing, path-tracer, brightness]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
---

# What happened

- The path tracer has no accumulation, so references were made by
  averaging 240 screenshots. The screenshots were tonemapped (AgX) and
  8-bit: noise through a compressing curve and clipping averages darker.
  The mean depended on the samples per pixel (3 times on a ceiling between
  4 and 16 spp), and DDGI looked 3 to 7 times too bright.
- The test scene had auto exposure. Turning reflections off made the
  mirror show the bright sky, the exposure dropped, and everything else
  looked darker: it looked like the reflections added light.
- Some runs had the camera turned by mouse events at window creation; the
  comparison points then sampled other surfaces.

# Rule

- Compare brightness in linear HDR (linear tonemapper, HDR viewport, read
  float pixels) at a fixed exposure. A reference must give the same mean
  at different sample counts.
- Pin auto exposure off for any A/B test, as in
  [uncontrolled-test-conditions](uncontrolled-test-conditions.md).
- Check that the views of an A/B pair are identical before comparing
  values (look at the pair side by side).
