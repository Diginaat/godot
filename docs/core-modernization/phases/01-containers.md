# Phase 1 — containers (in progress)

Inherited prototype uses packed COW storage plus STL algorithms, not std::vector
storage. Preserve it unstaged; numeric hash regressions are tracked separately.

Completed initial slice: original-source LocalVector versus std::vector with an
experimental Godot allocator. Seven sizes (0/1/16/256/4K/32K/1M), eight paired
workloads and basic lifetime/deep-copy/COW/clear/reset/self-push contracts.
Timing is uninstrumented; separate untimed Memory-TU CRT hooks count allocations,
reallocations/frees and usable peak bytes. No engine allocator was changed.

Windows correctness checkpoint: trait-aware bulk copying in LocalVector's Span constructor
from 256 trivially copyable elements; small/nontrivial copies retain their loop.
Eleven alternating process pairs support ~2.4x/4.4x faster copies at 256/4K ints,
with unchanged allocations. Empty/single-copy costs rise ~0.24/0.67 ns; 32K has
an unrelated STL-control confounder. No blanket speedup or STL storage adoption.
Details: [KB evidence](../../../kb/project/unique-array-evaluation.md).

**Performance acceptance blocked by follow-up boundary sweep.** Twelve sizes,
eleven alternating process pairs: 255/256/257 copies measure 124.79/125.43/124.63
ns originally versus 136.25/60.48/57.60 ns after the change. Small-copy regressions
range ~9–33% in this harness (64 ints: 53.20 -> 70.68 ns), with close STL controls.
This supersedes the earlier near-neutral small-copy observation; do not infer
engine-wide slowdown or zero regression from either microbenchmark alone.
No engine source changed during this follow-up and the threshold was not retuned.

MSVC production editor build completed successfully (15m09s) with tests=yes.
Focused core selection: **279 tests passed, 3,803,093 assertions, zero failures**.
Tests add non-default-constructible copy lifetimes, independent trivial copies,
empty spans and the bulk path. The engine build used the dirty working tree,
including inherited prototypes; the benchmark isolates the LocalVector header
against original sources. Neither result proves publication readiness or a clean
cross-platform engine build. OOM, over-alignment, full aliasing and general
element coverage remain open. No global container replacement accepted.

Accept only a reproducible benefit with no relevant regression. Keep shared
Vector/PackedArray/String layouts unchanged until compatible storage is proven.
Do not remove CowData merely because it is an old implementation.

Gate: isolated build, focused lifecycle tests, relevant sanitizer/configuration
coverage, baseline comparison and scoped commit. This small Windows-validated
copy change is a checkpoint, not completion of Phase 1 or platform acceptance.

Next: inspect small-copy code generation/layout and measure representative caller
patterns before accepting, retuning or explicitly reverting the copy policy.
Then add element types and compiler/sanitizer gates. Do not change the threshold
blindly or infer whole-engine improvements from the copy microbenchmark.

```powershell
python devtools/test/unique_array_bench.py
python devtools/test/unique_array_bench.py --candidate-local
python devtools/test/unique_array_bench.py --compare-copy
python devtools/test/unique_array_bench.py --compare-copy --boundary
```

Python 3.12+ and configured SCons profiling header required. All generated sources,
executables and raw data remain in ignored devtools/out/unique_array_bench.
