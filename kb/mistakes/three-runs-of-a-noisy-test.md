---
type: Mistake
title: Credited a fix on the strength of 3 runs of a noisy test
description: The flicker test has outliers even on an unchanged build; a fix judged on 3 runs (16 frame window) turned out not to be the fix at 6 runs.
tags: [mistake, measuring, flicker, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The flicker test (`--measure=60` after 600 frames, pixels deviating more
than 2 levels) gave 0.07% and 0.49% with a 4 frame scheduler window, and
0.03-0.06% in 3 runs with a 16 frame window. The 16 frame window was
committed and documented as the fix. Later, 6 runs per build showed:

- the unchanged baseline also has outliers (0.14% and 0.19%);
- the 16 frame build still reached 0.61%;
- the real cause was the 3x update rate
  ([the finding](/ddgi/findings/scheduler-favored-set-stability.md)).

The DDGI.md entry and the kb had to be corrected.

# Why

The spread of the test on an unchanged build wasn't known. 3 good runs of
a test with rare outliers prove little.

# Rule

- Know a test's spread on the baseline before using it to judge a change.
  For the flicker test: 6 runs, and compare `flicker_std` (stable) as well
  as the pixel share (outliers).
- When a later measurement contradicts a committed claim, correct the
  claim where it was written (code comments, DDGI.md, kb) in the next
  commit.
