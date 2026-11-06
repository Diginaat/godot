---
type: Mistake
title: A benchmark ran with the feature it measures inactive
description: The first HZB bench gave 225 CLOD objects one shared mesh and material; Godot batched them into one render element, which CLOD selection skips, and the GPU timings were not being measured either.
tags: [mistake, performance, testing, clod]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-07T01:00:00+01:00 }
---

# What happened

The first `hzb_bench` run finished on both backends and "passed": 12 rows.
All GPU times were 0.000 (the viewport's render-time measurement was never
switched on), the depth and opaque stage times were 0 (those names come from
`RENDER_TIMESTAMP`, which only records while the profiler runs), and the CLOD
counters were 0 in every mode. The 225 dense objects shared one mesh and one
material, so the render list merged them into an element with `repeat > 1`,
which `_prepare_mesh_clod()` skips.

# Rule

A benchmark checks that it measured something before it prints numbers: the
feature's counters are nonzero in the modes that enable it, and every timing
it reports is above zero. `hzb_bench.gd` now prints `HZB_BENCH_FAIL` otherwise.
For CLOD scenes, give each object its own material (or mesh) so instances are
not batched. Use direct `RD::capture_timestamp()` names, not `RENDER_TIMESTAMP`,
for stages a script must read.
