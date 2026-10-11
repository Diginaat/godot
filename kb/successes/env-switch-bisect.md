---
type: Success
title: Narrowing a GPU ordering problem with render graph switches
description: GODOT_RG_REORDER=0 and GODOT_RG_FULL_BARRIERS=1 showed within two runs that the DDGI bounce loss was a synchronization problem, not the new pass's data.
tags: [success, debugging, render-graph, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What worked

Adding XeGTAO before the DDGI apply pass made the probes lose their bounce
light. Many data-side tests changed nothing: AO values, resolution, a
dummy normal buffer.

Two runs with environment switches in `rendering_device.cpp` each fixed
it:

- `GODOT_RG_REORDER=0` (no reordering);
- `GODOT_RG_FULL_BARRIERS=1` (full barriers).

So the cause was in the render graph's derived synchronization. Moving
XeGTAO after the GI pass shipped as the fix. Details:
[the finding](/ddgi/findings/render-graph-bounce-loss.md).

# Reuse

When a new pass changes another system's result without sharing data with
it, try the two switches first. They take minutes and tell ordering
problems from data problems.
