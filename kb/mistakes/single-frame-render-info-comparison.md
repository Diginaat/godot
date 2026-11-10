---
type: Mistake
title: A single-frame render info comparison passed by timing luck
description: Comparing one frame's shadow render info between the GPU path and the CPU path matched only at some frame offsets and hid a real double count; sums over whole cycles of frames are exact.
tags: [mistake, testing, render-info, gpu-instances]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-10T18:00:00+01:00 }
---

# What happened

`gpu_instances.ps1` compared the shadow render info of the last frame with
and without the GPU path. Which shadow passes a frame draws repeats (lights
moving in a three-frame cycle, far cascades kept for several frames), and
the GPU path's count is read back a few frames late. Adding one phase to
the fixture shifted the frame offsets and the positional-shadow comparison
failed (5584 against 5492). The same offsets with the new phase removed
failed too, so the old pass had been luck.

Summing 12 frames on each path made every phase exact except the dual
paraboloid light, which exposed a real bug: `_fill_instance_data()` counted
earlier passes' elements again when the instance buffer was remapped in the
middle of a frame (a pass without CPU elements leaves it unmapped).

# Rule

Compare per-frame counters that are read back late, or that depend on which
passes a frame draws, as sums over a whole number of cycles (12 frames
covers cycles of 1, 2, 3, 4 and 6). To find which part differs, turn the
parts of the scene on one at a time with an environment switch and print
per-frame series and per-pass counts in one build.
