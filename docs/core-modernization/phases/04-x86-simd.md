# Phase 4 — x86 SIMD (in progress)

## DynamicBVH volumes in SSE (D014), 2026-11-16

The profiles ([KB](../../../kb/project/core-cpu-profiles.md)) put
`DynamicBVH` at 9% of the node-churn main thread (`get_proximity_to`,
`merge`, `select_by_proximity`, `contains`: tree updates of moving
instances) and 5% of the Overgrown Subway render thread (`intersects_convex`,
`intersects`: convex queries of the scene cull's indexers). Its `Volume` is
private, so its layout can change without any ABI effect.

On x86-64 single precision, `Volume` keeps `min` and `max` in aligned
16-byte slots (a padding float after each, `alignas(16)`, 32 bytes;
static-asserted), and `contains`, `merge`, `is_not_equal_to`,
`get_proximity_to`, `intersects` and the convex test use SSE2. Every kernel
does the scalar operations in the scalar order, so results and the tree's
shape are bit for bit the scalar code's: `minps`/`maxps` return the second
operand on NaN and equal values like `a < b ? a : b`, sums run x, y, z,
negation flips the sign bit (`-0.0` for zero half-extents). `convex_query`
converts the caller's planes once per query into registers (the caller's
array has no guaranteed alignment; up to 32 planes, the scalar test above
that). Nodes grow from 48 to 64 bytes. Double precision and other CPUs keep
the scalar code.

| ms per frame, median of 5 alternating rounds | Base (D013) | SSE | Scalar control |
| --- | --- | --- | --- |
| `core_workload.gd --mode=nodes` | 14.69 | 14.49 | 14.87 |
| `core_workload.gd --mode=instance` | 11.87 | 12.18 | 12.04 |

The scalar control is the same source with `DYNAMIC_BVH_SSE` off: SSE
against it is -0.38 ms (-2.6%) on node churn, lower in every round, and
-0.01 ms (median of per-round differences, both signs) on instancing,
which does not use the BVH much. The control itself is 0.17 ms slower than
the base on instancing: a code layout effect of rebuilding, which a first
two-way A/B had counted as a regression of the SSE code. Test:
`[DynamicBVH] Queries against brute force ...` (600 boxes through inserts,
updates, removes, the three optimizers and degenerate boxes; box and convex
queries against a brute force with the scalar tests, so the SSE kernels must
take the same decisions). 1,477 cases pass.

## Earlier exploration

Initial explicit packed Vector3 dot/cross
candidates are ~41% slower locally; do not integrate them. Resident private SoA
has promising throughput, but conversion reverses its advantage in this test.

Next: profile actual math call sites and data lifetimes before selecting a
kernel. Keep common mathematical contracts architecture-neutral. Target SSE4.2
only in an explicit optimized configuration/dispatch; no global ISA flags.
Use aligned working data, safe component packing and no unaligned intrinsics.

Gate: float/double, constexpr, integer/NaN/Inf/zero/signed-zero semantics,
debug/release, unsupported CPU handling, benchmark latency and batch conversion
costs, isolated SCons integration. Then commit a proven component, not all vectors.
