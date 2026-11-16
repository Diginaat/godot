---
type: Success
title: A control build of the same source separates a change from code layout
description: A two-way A/B blamed the DynamicBVH SSE change for a 3% slowdown; a third build with the change switched off showed the same slowdown, so it was code layout from rebuilding, and the change itself was a 2.6% gain elsewhere.
tags: [success, performance, measuring, builds]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-16T01:10:00+01:00 }
---

# What happened

The DynamicBVH SSE kernels (D014) measured -1.9% on node churn and +3.2%
on scene instancing against the previous build, every round. A profile of
both builds showed no DynamicBVH time at all on the instancing main thread,
only small shifts in unrelated functions. A third build of the same source
with the SSE path switched off (`#if 0` on its guard, everything else equal)
went into the same alternating A/B:

| ms per frame | Previous build | SSE | Control (SSE off) |
| --- | --- | --- | --- |
| Node churn | 14.69 | 14.49 | 14.87 |
| Scene instancing | 11.87 | 12.18 | 12.04 |

The control was as slow as the SSE build on instancing: rebuilding moved
that workload by about 1.5% through code layout alone. Against the control,
the SSE change is -2.6% on node churn and neutral on instancing.

# When to use it

Whenever a change shows a few percent of regression (or gain) in a
workload whose profile does not contain the changed code. Build the same
source with the change compiled out, keep both binaries, alternate all
three in one A/B. A control build of a header-only change can be quick
(1-2 minutes when the header is included by few files). Profiling a
renamed binary needs its PDB under the original name in a separate symbol
directory (`devtools/profile/profile.ps1 -SymbolPath`).

# Renderer-bound runs (scene runtime M4, 2026-11-16)

The same check with an unused function added to `node_3d.cpp` of the
unchanged source: windowed MeshInstance3D scenarios (100k BVH updates per
frame) were 2-11% slower than the original build. Renderer-bound,
memory-bound frames are more layout-sensitive than the 1.5% above; see
[the mistake it caused](/mistakes/renderer-bound-frame-for-scene-changes.md).
