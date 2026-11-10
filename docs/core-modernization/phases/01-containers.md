# Phase 1 — containers (in progress)

Inherited prototype uses packed COW storage plus STL algorithms, not std::vector
storage. Preserve it unstaged; numeric hash regressions are tracked separately.

Completed initial slice: original-source LocalVector versus std::vector with an
experimental Godot allocator. Seven sizes (0/1/16/256/4K/32K/1M), eight paired
workloads and basic lifetime/deep-copy/COW/clear/reset/self-push contracts.
Timing is uninstrumented; separate untimed Memory-TU CRT hooks count allocations,
reallocations/frees and usable peak bytes. No engine allocator was changed.

Narrow candidate: trait-aware bulk copying in LocalVector's Span constructor
from 256 trivially copyable elements; small/nontrivial copies retain their loop.
Eleven alternating process pairs support ~2.4x/4.4x faster copies at 256/4K ints,
with unchanged allocations. Empty/single-copy costs rise ~0.24/0.67 ns; 32K has
an unrelated STL-control confounder. No blanket speedup or STL storage adoption.
Details: [KB evidence](../../../kb/project/unique-array-evaluation.md).

Editor build and focused regression tests are running for the candidate. Tests
add non-default-constructible copy lifetimes, independent trivial copies, empty
spans and the bulk path. OOM, over-alignment, full aliasing and general element
coverage remain open. Do not accept a global container replacement.

Accept only a reproducible benefit with no relevant regression. Keep shared
Vector/PackedArray/String layouts unchanged until compatible storage is proven.
Do not remove CowData merely because it is an old implementation.

Gate: isolated build, focused lifecycle tests, relevant sanitizer/configuration
coverage, baseline comparison and scoped commit. Candidate not promoted yet.

```powershell
python devtools/test/unique_array_bench.py
python devtools/test/unique_array_bench.py --candidate-local
python devtools/test/unique_array_bench.py --compare-copy
```

Python 3.12+ and configured SCons profiling header required. All generated sources,
executables and raw data remain in ignored devtools/out/unique_array_bench.
