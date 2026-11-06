---
type: Mistake
title: GPU stage check read timestamps after a texture readback
description: A smoke test saw zero captured RD timestamps because it called get_image() first; the readback stalls and begins a new frame, which replaces the timestamp results.
tags: [gpu, testing, profiling, timestamps]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T00:00:00+01:00 }
---

# Observed failure

The valley fly-through smoke reported that GPU culling, cluster LOD and HZB
never dispatched. `RenderingDevice.get_captured_timestamps_count()` returned 0
for every sample, while the dense-forest test on the same binary saw all three
stages. The fly-through called `Viewport.get_texture().get_image()` and only
then read the timestamps on the render thread.

# Cause

`RenderingDevice::texture_get_data()` calls `_flush_and_stall_for_all_frames()`,
which ends the frame and runs `_begin_frame()`. `_begin_frame()` replaces the
current slot's timestamp results with that slot's (empty) capture count. The
stages had run; the test read the results of a frame without any work.

# Rule

Read RD timestamps (and any other per-frame profile data) before any texture or
buffer readback in the same frame. Treat "no timestamps at all" as a measurement
problem first: Godot's own renderer timestamps are only captured while
profiling, so an empty list does not prove that custom stages were skipped.
