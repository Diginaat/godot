# Phase 4 — x86 SIMD (exploration only)

No core/math implementation changed. Initial explicit packed Vector3 dot/cross
candidates are ~41% slower locally; do not integrate them. Resident private SoA
has promising throughput, but conversion reverses its advantage in this test.

Next: profile actual math call sites and data lifetimes before selecting a
kernel. Keep common mathematical contracts architecture-neutral. Target SSE4.2
only in an explicit optimized configuration/dispatch; no global ISA flags.
Use aligned working data, safe component packing and no unaligned intrinsics.

Gate: float/double, constexpr, integer/NaN/Inf/zero/signed-zero semantics,
debug/release, unsupported CPU handling, benchmark latency and batch conversion
costs, isolated SCons integration. Then commit a proven component, not all vectors.
