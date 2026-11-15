---
type: Mistake
title: Separate runs of an A/B had an order bias, and a draw count was taken for the cost
description: On a game level the second process of a pair ran 0.2-0.3 ms slower whichever variant it was, and halving the GPU path's draws did not touch its 0.3 ms loss.
tags: [mistake, measuring, gpu-instances, renderer]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-15T00:00:00Z }
---

# What happened

The GPU instance path was slower than the CPU path on a game level with
many meshes and few copies each, with twice the draws. The empty per-LOD
draws were recorded as the probable cause. Removing them brought the draws
to the CPU path's count, and the GPU time stayed 0.25-0.3 ms worse. A
whole-frame timestamp breakdown, then the same comparison without the omni
lights' shadows, put the loss in the fixed cost of about 12 small shadow
passes.

On the way, A/B pairs of separate processes gave opposite answers
depending on which ran first: the second run of a pair was 0.2-0.3 ms
slower, a bias as large as the effect.

# Rule

- Do not credit a cost to the counter that changed (draws, primitives);
  remove that and measure again, or break the frame down by GPU timestamps
  first.
- For differences under about 0.5 ms, alternate the setting inside one
  process (short blocks, settle frames after each switch, many blocks per
  view) and run an A/A of a no-op setting first to know the floor. If
  separate runs are needed, run both orders and average.
