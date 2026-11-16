---
type: Mistake
title: Judged a scene-side change on renderer-bound whole-frame time
description: The transform propagation early-out (scene runtime M4) looked like a 10-12% regression in windowed MeshInstance3D runs; a build of the old source with only a padding function was 4-11% slower too, the profile slowed down uniformly in renderer code the change never ran, and headless runs with a dummy renderer measured the change itself.
tags: [mistake, performance, measuring, builds, scene-runtime]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-16T20:00:00+01:00 }
sources:
  - id: roadmap
    title: Scene runtime roadmap, M4
    url: /docs/implementation/scene-runtime-roadmap.md
---

# What happened

M4 lets Node3D transform propagation stop at children that are already
dirty. The `-Visual` scenario benchmark (MeshInstance3D, a window,
Vulkan) showed `wide` 10-12% and `parents` 4-8% slower per frame, every
round, while the scenarios' own operations (`op`) moved by 2-5%. A
temporary counter showed the early-out never triggered in `wide` and
`parents`. A sampling profile of both builds showed every part of the frame
slower by the same share, including `DynamicBVH` and
`RendererSceneCull::instance_set_transform`, which M4 does not touch. The
time was spent in the renderer's scene update (100k BVH updates per frame),
which is memory-bound and sensitive to code layout.

A control build of the previous source with one unused function added to
`node_3d.cpp` (`m3pad`) was 2-11% slower than the previous build on the same
scenarios. Running the same MeshInstance3D scenarios headless (dummy
RenderingServer, `--headless ... --visual`) removed the renderer and showed
the real per-notification cost of the first M4 designs (50-75 ns per
notified node in `wide`) and that the final design has none.

Several rounds of redesign and rebuilds were spent chasing the windowed
numbers before this.

# Rule

For a change in scene code (nodes, notifications, propagation), judge on
the scenario's `op` time and on headless runs (with `--visual` for the
MeshInstance3D notification path), against a control build of the
unchanged source with a code layout change. Use windowed `-Visual` frame
time only for effects larger than the layout spread measured that day, and
report it as such. When the profile slows down uniformly in code the change
does not run, suspect layout before the change.
