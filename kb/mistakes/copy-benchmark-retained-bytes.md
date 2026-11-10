---
type: Mistake
title: Copy benchmark omitted retained byte reporting
description: Copy cases printed zero live requested bytes because only growth cases populated the field; untimed measurement now records it and enforces parity.
tags: [core, benchmarks, memory, measurement]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
---

The original unique-array benchmark filled `live_requested_bytes` only for
append/resize cases. Copy cases left it zero even while source and copy buffers
were allocated. Allocation counts and CRT peak tracking were separate and
still measured; a zero retained-byte field was not evidence of zero memory.

Copy cases now snapshot retained Godot-requested bytes only in the untimed
count binary, subtracting the prepared source fixture. Timing code has no added
per-copy measurement calls. The paired runner asserts matching allocator events,
capacity, requested bytes and CRT peak deltas between original and candidate.

Rule: validate every reported metric per workload, not just the output schema.
Use untimed instrumentation and verify parity without distorting timed loops.
Historical artifacts remain intact; corrected fields belong to the follow-up run.
