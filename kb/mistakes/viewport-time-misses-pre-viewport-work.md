---
type: Mistake
title: The viewport's GPU time missed work recorded before the viewport
description: VoxelField3D's selection runs through call_on_render_thread() before the viewport renders; the valley bench reads the viewport's render time, so 3.1 ms of selection went unmeasured and the field was reported as nearly free.
tags: [mistake, performance, measuring, gpu]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-08T21:00:00+01:00 }
---

# What happened

The GPU voxel field was measured with the valley bench, which reports the
viewport's measured render time (GPU p50). It came out at -0.05 to +0.71 ms
against the static proxies. The field's selection is queued with
`call_on_render_thread()` and runs before the frame's viewports, outside
that measurement. A whole-frame timestamp profile later showed the field
cost 4.9 ms more, 3.1 ms of it in the selection, serialized by one GPU
timestamp per field ([the page](/project/far-field-gpu-voxel-levels.md)).

# Rule

- Measure GPU cost over the whole frame, not the viewport's render time,
  when a feature records GPU work outside the viewport (render-thread
  callbacks, compute before drawing, readbacks):
  `devtools/test/gpu_intervals.ps1` prints every timestamp interval and the
  frame total.
- Never capture a GPU timestamp per object or per instance. A timestamp
  orders the render graph around it: everything before finishes, nothing
  after starts. One per object turns parallel small jobs into a chain.
