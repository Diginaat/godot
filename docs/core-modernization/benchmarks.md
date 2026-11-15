# Benchmarks

## Engine workloads (2026-11-16)

`devtools/profile/core_workload.gd`, vsync off, editor binary
(`production=yes debug_symbols=yes`), Ryzen 5 3600, RTX 3060, Vulkan, 15 s
per run, builds alternating, order reversed every other round. Profiles of
where the time goes: [KB](../../kb/project/core-cpu-profiles.md).

| Change | Workload | Before | After |
| --- | --- | --- | --- |
| D012 lock-free `RID_Alloc::owns()` on x86-64 | `instance` | 13.83 ms (13.40-15.51) | 12.37 ms (12.14-14.78) |
| D012 | `nodes` | 14.95 ms (14.62-15.60) | 14.91 ms (14.72-14.96) |
| D013 one-block `HashSet`, `DependencyTracker::update_end()` | `instance` | 12.58 ms (12.50-12.73) | 11.69 ms (11.61-11.77) |
| D013 | `nodes` | 14.84 ms (14.80-14.98) | 14.59 ms (14.48-14.66) |

The baselines of D013 are the D012 build. Large-set microbench for D013
(`core_templates_bench.py --baseline HEAD`, 11 samples): HashSet insert /
hit / churn 51.47 / 15.34 / 41.50 to 52.64 / 15.80 / 40.30 ns, live bytes
equal; unchanged code within 0.95-1.04x except String copy/detach (1.14x,
the standalone harness effect recorded earlier).

## Recorded exploratory evidence (before Phase 0 commit)

Baseline `172e099b95307708005dafcf719693d20b1e7088`; uncommitted candidate.
Windows x86-64, Ryzen 5 3600, MSVC, C++17 `/O2 /MT`, no LTO. Containers use
DEBUG_ENABLED allocation accounting; math uses `/fp:precise`. CPU frequency and
affinity were not pinned. Compilers/SCons excluded during timing.

| Case | Baseline → candidate | Interpretation |
| --- | --- | --- |
| Integer AHashMap hit | 8.76 → 22.67 ns | Reject adapter as general speed upgrade |
| Integer AHashMap churn | 22.43 → 59.14 ns/op | 2.64x slower |
| Numeric hash-container live bytes | +50–57% | Not allocator overhead, peak bytes or RSS |
| 128-character String-key hit | 182.54 → 111.56 ns | Gain in synthetic shared UTF-32 key workload |
| 64 KiB byte hash | 19.74 → 2.29 us | Legacy Murmur3 versus wyhash/XXH3 dispatch |
| Packed Vector3 dot | 0.870 → 1.230 ns/vector | Tested explicit SSE candidate ~41% slower |
| Packed Vector3 cross | 1.224 → 1.724 ns/vector | Tested explicit SSE candidate ~41% slower |
| Dot with AoS→SoA conversion + SSE | 1.947 ns/vector | Conversion paid inside timing |
| Resident SoA dot, compiler C++ / explicit SSE | 0.211 / 0.402 ns/vector | Conversion excluded; not an engine-wide speedup |

Containers: 24 workloads, 11 separate-process alternating pairs, warmup pair
excluded, checksum/allocation cleanup checks. Integer keys N=32,768; String
keys N=4,096. Historical raw source manifest and all p50/p95/MAD values are in
`devtools/out/core_templates_bench/`; fuller historical notes currently remain
uncommitted in `kb/project/core-template-benchmarks.md`.

Math: seven cases, N=16,384 float32 vectors, 128 repetitions, one warmup, 11
samples with reversed case order in one process. All 81,920 finite-component
candidate comparisons passed tolerance checks before timing. MAD <0.5% for
these samples. Not a full mathematical semantics or latency suite; no double,
integer, NaN/Inf/signed-zero or ARM runtime coverage. Results and source hashes:
`devtools/out/math_simd_bench/results.json`.

## Reproduce

```powershell
python devtools/test/core_templates_bench.py --baseline 172e099b95307708005dafcf719693d20b1e7088 --build-only
# No other builds during measurement:
python devtools/test/core_templates_bench.py --skip-build --samples 11
python devtools/test/math_simd_bench.py
```

These scripts compile actual headers and necessary source with MSVC for isolated
experiments. Godot itself remains SCons-built. No thirdparty install is needed
on the original baseline. Configure/build with SCons first to provide
`core/profiling/profiling.gen.h`; the runner fingerprints that generated input.
New buffer hashing is selected only when its declaration
exists in that variant. Comparing an unchanged checkout to itself measures noise,
not modernization gains. Do not relabel that control as the historical candidate.

Phase 0 isolation recheck (original HEAD source snapshot, only benchmark files
overlaid): both baseline/control executables compile and pass all 24 workloads.
Math probe reproduces slower SSE dot/cross (0.869/1.228 and 1.223/1.720 ns/vector).
These are infrastructure/control checks, not new engine implementation results.

## Required coverage expansion before implementation acceptance

| Area | Missing work |
| --- | --- |
| Containers | N=0/1/16/256/4K/32K/1M; copy/move/resize, reserve, iteration, random access; allocation counts/peaks |
| Maps × hashers | Original Godot, robin_hood flat/node, unordered_dense normal/segmented; native defaults and matched hash policies; full adapter costs |
| Hashes | Both wyhash and XXH3 at identical lengths, alignment edges, seeds, short/long Unicode and custom types |
| Operations | Varying hit rates, insert/erase probabilities, reserve policies, heavy collisions and large values |
| Strings | Construction/concat/compare/find, UTF conversion, StringName interaction, temporaries |
| Math | Single-operation latency versus throughput, vectors/matrices/transforms/quaternions, both precisions, exceptional values |
| Engine | Scene loading, nodes, rendering, physics, editor interaction, large worlds; fixed fixtures, no mutation of user projects |
| Platforms | GCC/Clang/Linux, ARM64; sanitizers and debug/release coverage |

Budget each experiment and retain negative results. Use confidence/spread, not
one noisy percentage. Establish per-workload acceptance criteria before changing
implementations. Archive commands, code identity, CPU/compiler/flags, dataset,
checksums, memory metric definition and raw samples.
