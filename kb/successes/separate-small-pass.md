---
type: Success
title: Side work in its own small pass
description: Moving probe marking out of the DDGI apply pass into a pass with one thread per 8x8 tile cut its cost from 0.05 to 0.03 ms at 1080p.
tags: [success, performance, shaders, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What worked

The scheduler needs to know which probes visible surfaces sample. Done
inside the apply pass, it cost 0.05 ms at 1080p however few pixels marked.
A separate compute pass does the same:

- one thread per 8x8 tile;
- one pixel per tile, at a position that rotates every frame;
- the same depth and normal buffers as the apply pass.

It costs about 0.03 ms. The apply pass keeps its original speed.

# Reuse

Low-density bookkeeping (feedback for a scheduler, statistics, tile
classification) goes in its own small dispatch, not in a hot full-screen
shader. See [the finding](/ddgi/findings/shader-code-cost-without-work.md).
