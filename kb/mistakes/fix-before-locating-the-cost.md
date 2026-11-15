---
type: Mistake
title: Built the fix for a suspected cost before timing the stages
description: Removed the GPU instance path's per-view command uploads as the suspected GPU cost on a game level; GPU time did not change, and a stage timestamp then showed the cost was in the draws.
tags: [mistake, performance, gpu-instances, measuring]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-15T23:30:00+01:00 }
---

# What happened

On the Overgrown Subway level the GPU instance path cost 0.05-0.2 ms more
GPU time than the CPU path. The commands it uploaded per view and per
shadow pass (about 80 KB each) looked like the cause, so they were
replaced by GPU clears (D039) and measured: 12 alternating runs, no change
beyond the spread. Only then did a profiler timestamp after the shadow cull
dispatch split the batch's interval into the cull (about 7 us per batch)
and the draws after it, and show that the draws themselves (+0.03-0.05 ms
in shadows, +0.02-0.03 ms in the depth pre-pass) were the difference.

# Rule

Before building a fix for a GPU cost, add `RENDER_TIMESTAMP`s around the
suspect (start and end) and compare `--gpu-profile` intervals of the two
paths on the safe render thread. If the suspect's own interval is small,
look elsewhere first. The fix here was kept (less CPU work, no uploads),
but it did not answer the question it was built for.
