---
type: Mistake
title: Metrics that measured noise instead of the effect
description: Settle-time thresholds of 2-10% of a small change, and a 90% target below the noise floor, measured run-to-run noise, not the response.
tags: [mistake, measuring, metrics]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

- **Storm game level:** settle time was "frames until within 10% / 2% of
  a 1-3 level change". Slow ±0.5-level drift from rain and probe noise
  decided the result. The new build looked 3-4x slower, but the traces
  showed both builds reach the new level within 30 frames.
- **Emissive step test:** "frames to 90% / 98%" against the mean of the
  last 30 frames. The per-pixel frame noise is about 10% of the step, so
  98% was never reached and 90% landed in the noise.
- **Single frames in the game level:** frames compared between runs
  differed by 15% at the same object position, because the level isn't
  frame-deterministic.

# Why

The thresholds were picked without first measuring the noise of the
quantity.

# Rule

- Measure the noise floor first: the same build, repeated runs, and the
  quantity with nothing changing. Pick thresholds well above it, or
  subtract it (the emissive step test subtracts the mean error of the
  settled frames).
- Look at the raw trace before trusting a summary number.
- Compare averages and their spread, not single frames, when runs aren't
  deterministic. See [the test protocol](/tooling/ddgi-test-protocol.md).
