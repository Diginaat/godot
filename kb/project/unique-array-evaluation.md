---
type: Finding
title: Unique array baseline and bulk-copy candidate
description: LocalVector/STL allocation and copy measurements; Windows correctness passes, but a follow-up boundary sweep blocks performance acceptance.
tags: [core, containers, allocation, benchmarks, compatibility]
status: draft
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
sources:
  - id: runner
    resource: ../../devtools/test/unique_array_bench.py
    title: Baseline snapshot and copy-pair runner
  - id: workload
    resource: ../../devtools/test/unique_array_bench.cpp
    title: Contract checks and measured workloads
  - id: phase
    resource: ../../docs/core-modernization/phases/01-containers.md
    title: Phase 1 acceptance state
---

# Scope and baseline

Baseline `172e099b95307708005dafcf719693d20b1e7088`, archived original headers
and Memory source; candidate overlays **only** working-tree `local_vector.h`.
No inherited hash/String/COW prototype participates. Windows x86-64, Ryzen 5
3600, MSVC `/O2 /MT`, C++17, DEBUG_ENABLED, no LTO. CPU affinity/frequency not
pinned. The driver refuses measurement while compilers/SCons are active.

Initial matrix: seven sizes (0, 1, 16, 256, 4,096, 32,768, 1,048,576 integers)
and eight paired operations: append, reserved append, initialized resize, copy,
copy-then-move, sequential traversal, deterministic permuted access, copy-then-
pop-all. Setup is excluded for prepared read fixtures; create/copy/destruction
costs are included where named. This is 56 paired workloads, not comprehensive
coverage of every container or element category.[^runner][^workload]

Contract checks cover deep-copy independence, moves, nontrivial lifetime,
capacity-preserving clear, explicit reset, self-push, initialized integer resize
and original shared Vector COW detachment. They do not prove all aliases,
OOM/exception handling, over-aligned types or all nontrivial relocatability.

# Allocation measurement

Timing binary compiles original Memory code **without hooks**. A second untimed
binary force-includes CRT malloc/calloc/realloc/free hooks only in memory.cpp.
Measured standard vectors allocate through an experimental Godot allocator;
standard-library fixture allocations do not participate. Hooks leave the real
allocator implementation intact. Counts include all repeated cycles, excluding
prepared source fixtures. Count/timing checksums must match.

`live_requested_bytes` comes from Godot accounting. `crt_peak_delta` tracks
CRT `_msize` usable sizes including engine headers; it is neither RSS nor
physical process peak. Allocation/reallocation/free events are separate.
For 32,768-element unreserved append, each original LocalVector cycle uses one
allocation and 24 reallocations; the tested MSVC std::vector uses 27 allocations
and no reallocations. Its growth cannot automatically substitute for Godot's
byte-relocating realloc behavior. No production STL storage adoption follows.

# Narrow candidate

Original LocalVector Span constructor copies elements individually. Reuse
existing `copy_arr_placement` only for trivially copyable spans with at least
256 elements; retain the original loop for small/nontrivial copies. No API,
layout, storage, allocator or COW change. This is not std::vector adoption.
Threshold is an initial local policy selected from tested integer sizes, not a
universal hardware optimum. Nontrivial types keep constructor/destructor paths.

An unconditional bulk-copy candidate was rejected: small sizes regressed
~19–31% in that experiment, while unrelated STL controls also shifted. Its
samples and exact candidate header remain under the ignored `copy/rejected_*`
artifacts. Do not pool that run with the thresholded candidate.

# Controlled copy-only results

Eleven alternating original/candidate process pairs, one warmup per process,
seven sizes. Larger copies process roughly 256 MiB per sample to avoid short
measurement intervals; small sizes cap repetitions at 32,768. p95 is nearest-
rank maximum of eleven observations. Unit: **ns per complete copy/destruction**.

| Integers | Original p50 | Candidate p50 | Original p95 | Candidate p95 | Candidate/original |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 2.390 | 2.628 | 2.548 | 2.792 | 1.100 |
| 1 | 37.933 | 38.599 | 38.071 | 39.835 | 1.018 |
| 16 | 44.308 | 44.266 | 46.802 | 45.670 | 0.999 |
| 256 | 126.385 | 52.701 | 128.598 | 59.698 | 0.417 |
| 4,096 | 1,524.786 | 347.400 | 1,533.148 | 352.039 | 0.228 |
| 32,768 | 25,972.363 | 9,634.619 | 26,313.037 | 10,393.115 | 0.371 |
| 1,048,576 | 1,019,729.688 | 814,082.813 | 1,030,420.313 | 830,675.000 | 0.798 |

All count/capacity/live-byte/CRT-peak measurements match between original and
candidate copy cases. All workload checksums match. Small empty/single copies
show 0.24/0.67 ns extra respectively; do not claim zero regression. At 32K,
the unrelated STL control differs markedly (46.45 -> 14.86 us), so layout/heap
effects remain a confounder for attribution of that particular size. The 256/4K
controls are much closer (49.48 -> 50.59 ns and 334.92 -> 346.78 ns), supporting
further validation of the sizeable native-copy improvement there. These results
are not engine frame-time improvements or allocator-independent guarantees.

Artifacts: `devtools/out/unique_array_bench/` baseline/candidate matrices and
`copy/paired.csv`, `copy/paired_results.json`, per-build source hashes and counts.
The candidate passed the MSVC production editor build with tests=yes (15m09s)
and **279 focused core tests / 3,803,093 assertions**, zero failures. The engine
build includes inherited uncommitted prototypes; standalone before/after
measurements isolate only the LocalVector header against original sources.
This is a narrow Windows checkpoint, not global Phase 1, cross-platform, full
test-suite or engine-workload acceptance. Threshold-neighborhood measurements,
more element types, sanitizer/OOM/aliasing gates and Linux/ARM remain open.[^phase]

# Follow-up: threshold boundary sweep

**Supersedes the earlier near-neutral small-copy observation.** Same original
commit and committed LocalVector header (`ee5973f764`), no engine edits. New
copy-only harness configuration adds 64/128/255/257/512 sizes; MSVC C++17 `/O2
/MT`, same CPU, 11 alternating original/candidate process pairs, one warmup per
process. Twelve sizes total. Non-power-of-two sizes are rejected outside copy-
only mode. Old seven-size results are retained separately.

| Integers | Original p50 ns | Candidate p50 ns | Candidate/original |
| ---: | ---: | ---: | ---: |
| 0 | 2.39 | 2.63 | 1.100 |
| 1 | 35.82 | 39.68 | 1.108 |
| 16 | 41.32 | 50.31 | 1.218 |
| 64 | 53.20 | 70.68 | 1.329 |
| 128 | 93.01 | 104.57 | 1.124 |
| 255 | 124.79 | 136.25 | 1.092 |
| 256 | 125.43 | 60.48 | 0.482 |
| 257 | 124.63 | 57.60 | 0.462 |
| 512 | 197.86 | 65.90 | 0.333 |
| 4,096 | 1,538.43 | 429.90 | 0.279 |
| 32,768 | 11,184.96 | 2,292.04 | 0.205 |
| 1,048,576 | 988,392.19 | 811,689.06 | 0.821 |

At 64 ints, MAD is 1.02% original / 0.77% candidate, versus 32.9% higher p50
time; STL control is 39.51 -> 39.90 ns. At 255/256/257, controls are also close.
That is sufficient to block general performance acceptance, but it does not
identify the cause or establish a real-engine caller regression. Different
executable/code layouts and allocator behavior remain possible contributors.
Do not average these results with the earlier run or hide the regressions behind
large-copy gains. Merely retaining the same small-copy source loop is not proof
of unchanged generated-code performance.

Allocation/reallocation/free counts, capacity, live requested bytes and CRT
peak deltas match before/after for all twelve sizes; this is now a mandatory
driver assertion, not just a manual post-check. Counter/timing checksums and
all standalone contracts pass. A reporting omission was fixed: the old copy
case did not populate `live_requested_bytes` (printed zero); the untimed binary
now captures the copy's requested bytes excluding source fixtures, with no
counter calls added to the timed copy loop. Historical zero fields were not
evidence of zero allocation or zero retained memory.

Artifacts: `devtools/out/unique_array_bench/boundary/copy/paired_results.json`,
`paired.csv` and per-variant manifests/counters. The previous editor test result
remains 279 passed; no full engine rebuild was needed for this tools-only change.
Next: inspect codegen and representative call sites, then decide whether to
retain, revise or explicitly roll back the policy. No silent retuning applied.

# Follow-up: specialized copy-only build (2026-11-10, recorded 2026-11-15)

`unique_array_bench.py --boundary --specialize-copy` compiles the other
workloads' control flow out of both executables (`ARRAY_BENCH_SPECIALIZE_COPY`),
same committed header (`ee5973f764`), same sizes and pairing:

| Integers | 0 | 1 | 16 | 64 | 128 | 255 | 256 | 257 | 512 | 4,096 | 32,768 | 1,048,576 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Candidate/original | 1.50 | 0.99 | 0.97 | 0.99 | 0.99 | 0.99 | 0.38 | 0.34 | 0.28 | 0.27 | 0.20 | 0.82 |

The 1-255 element "regressions" of the generic boundary sweep disappear when
the harness's own branches are gone, so they came from the harness's code
layout, not from the header. The size-0 ratio is 0.96 vs 1.43 ns, at the
timer's resolution. Decision: keep the 256-element bulk copy as committed.
Two other candidates (`local_vector_copy_candidate.py`: `std::uninitialized_copy_n`
for every size, and a single-element special case) were 7-15% slower at 1-16
elements in the generic harness and are not used.

