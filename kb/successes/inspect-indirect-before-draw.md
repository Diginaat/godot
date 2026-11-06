---
type: Success
title: Inspect indirect command bytes before submitting unsafe draws
description: A real-RD command test reproduced a sentinel overflow without asking the GPU to draw billions of instances.
tags: [rendering, indirect, testing, safety]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# What worked

Create native indirect MultiMesh resources without attaching a visible scene
instance. Read and assert every command word after CPU count changes. The
pre-fix `-1` sentinel reproduced as `4294967295` in both indexed/non-indexed
surface command counts, without submitting an unbounded draw.

After the native setter fix, the same checks pass. A test-only compute producer
then writes bounded counts and the ordinary Godot renderer produces the expected
material pixels. Vulkan and D3D12 both passed on one GPU. This is correctness
evidence, not a performance or large-world scalability result.

# Reuse

Validate indirect count/range/layout invariants before attaching geometry, then
test raster output. A hard frame limit and required success marker prevent a
parser failure or silent early exit from looking like a pass. Blocking buffer
reads belong in assertions, never the production per-frame visibility path.

Runner and conditions: [native validation record](../../knowledge/PERFORMANCE_BENCHMARKS.md).
