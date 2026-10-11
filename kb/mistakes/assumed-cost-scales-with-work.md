---
type: Mistake
title: Assumed shader cost scales with how many threads do the work
description: Built three ways of marking from fewer pixels in the apply pass before measuring the cost of the code alone.
tags: [mistake, performance, shaders]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

Probe marking inside the DDGI apply pass cost 0.05 ms at 1080p. Three
variants were each built and measured, one rebuild each:

- marking from one pixel per 2x2 block;
- marking from one pixel per 4x4 block;
- marking from a quarter of the workgroups.

None helped. Only then did a build with the code compiled out show that
the code being in the shader was the cost, not the stores. Details:
[the finding](/ddgi/findings/shader-code-cost-without-work.md).

# Rule

When added shader work costs more than expected, first measure with the
code compiled out against the code compiled in. That one test separates
"cost of the work" from "cost of the code being there". Do it before
trying variants.
