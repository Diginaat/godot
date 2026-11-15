---
type: Finding
title: Core template migration A/B microbenchmarks
description: The full migration made numeric hash containers slower and bigger and was dropped; the narrow String-key byte hash that replaced it is neutral on integers and 38-45% faster on String-key hits.
tags: [core, containers, stl, hashing, benchmarks, regression]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T18:09:00Z }
sources:
  - id: runner
    resource: ../../devtools/test/core_templates_bench.py
    title: Controlled actual-container A/B runner
  - id: workload
    resource: ../../devtools/test/core_templates_bench.cpp
    title: Workloads and fail-fast correctness checks
  - id: boundary
    resource: core-template-storage.md
    title: Migration compatibility boundary
---

# Outcome (2026-11-15)

The migration measured below was dropped
([storage decision](/project/core-template-storage.md)). What replaced it is
the byte hash alone: `hash_buffer()` used by HashMap/AHashMap/HashSet only
for String and CharString keys with the default hasher (`hash_table_key()`),
the old tables and every other hash unchanged. Same runner, same machine,
baseline `HEAD` (`80a7e37566`), two runs of 11 alternating pairs:

| Workload | Baseline p50 | Candidate p50 | Ratio (run 1 / run 2) |
| --- | ---: | ---: | ---: |
| HashMap insert / hit / miss / churn | 108.65 / 16.89 / 15.72 / 56.55 | 108.40 / 16.89 / 15.87 / 55.37 | 0.98-1.01 |
| AHashMap insert / hit / miss / churn | 42.11 / 8.51 / 12.14 / 21.81 | 41.82 / 8.59 / 12.06 / 22.38 | 0.99-1.03 |
| HashSet insert / hit / churn | 50.63 / 15.48 / 41.42 | 50.48 / 15.43 / 41.12 | 0.99-1.00 |
| AHashMap String hit, 16 characters | 51.66 | 32.00 | 0.62 / 0.62 |
| AHashMap String hit, 128 characters | 183.16 | 100.31 | 0.55 / 0.55 |
| Vector append | 5.81 | 6.30 | 1.09 / 1.11 |
| String copy/detach, 16 / 128 characters | 51.05 / 58.70 | 55.78 / 64.99 | 1.09-1.11 |

Live bytes are identical for every case. Vector append and String
copy/detach run code this patch does not change (`vector.h`, `ustring.h`
and `cowdata.h` are `HEAD`'s); their +9-11% repeated in both runs and is not
explained. It is a standalone-executable effect until an engine workload
shows otherwise (compare the LocalVector harness artefact in
[the unique-array evaluation](/project/unique-array-evaluation.md)).
Tests: full unit suite 1,474 cases / 483,264 assertions passed
(`tests=yes` production editor build, Windows, MSVC).

# Result (dropped migration, 2026-11-10)

**Do not treat this migration as a general performance upgrade.** Integer-keyed
HashMap/AHashMap/HashSet regress well beyond observed run-to-run variation, and
their live requested allocation bytes increase approximately 50–57%. Faster byte
hashing and String-key lookups do not offset that evidence automatically. Keep
the migration unpublished until its container indexing design is addressed.

# Conditions

- Baseline: `172e099b95307708005dafcf719693d20b1e7088` (`HEAD` before migration).
- Candidate: working-tree migration, same production files as the source manifest
  in `devtools/out/core_templates_bench/manifest.json`. Benchmark workload SHA-256:
  `1cadd6779b076ccf17e0c446ff4211c71767a19090cacdb510eb587f56d952db`.
- Windows x86-64, AMD Ryzen 5 3600 (6 cores/12 threads), MSVC toolset selected by
  the installed Visual Studio environment. C++17, `/O2 /MT /Gy /Gw /GR-`,
  `DEBUG_ENABLED`, `TOOLS_ENABLED`, no LTO, linker `/OPT:REF /OPT:ICF`.
- One discarded warmup pair; 11 measured pairs alternating candidate/baseline
  order. No compiler/linker/SCons active during measurement. CPU affinity and
  frequency were not pinned; background activity other than builds was not
  controlled. Most map/set median absolute deviations were below 1%.
- Actual Godot container headers and allocation/hash source implementations;
  only the two required String methods are extracted verbatim from each variant's
  `ustring.cpp` to avoid linking unrelated engine services. Diagnostics fail the
  run; none of the measured operations are stubbed.[^runner][^workload]
- Integer containers: 32,768 shuffled keys. Lookup: 32 rotated passes, 1,048,576
  operations. Insert: eight create/fill/destroy cycles (reported per insertion;
  destruction included). Churn: four full erase/insert cycles, reported per
  individual erase or insert. Container setup excluded from lookup timings.
- String map lookup: 4,096 distinct synthetic UTF-32 keys, 16 or 128 characters
  (64 or 512 hashed bytes), 32 rotated passes. Keys are shared COW copies, as in
  engine container insertion. These are not real asset-path distributions.
- Vector: append 32,768 integers (32 cycles); detach 4,096 integers (1,024 cycles);
  unvalidated sort 4,096 integers (128 cycles, including allocation/copy).
- String copy/detach: 32,768 copies with one character mutation, source retained.
- Byte hashes: repeated cache-hot inputs at the listed byte lengths, unaligned
  by one byte, varying seeds. Baseline Murmur3 versus candidate length-dispatched
  wyhash/XXH3. This does **not** isolate wyhash versus XXH3 or establish an optimal
  256-byte dispatch threshold.

# Timings

Times are **ns per operation**. Ratio is candidate p50 / baseline p50: greater
than 1 is slower. p95 uses nearest rank (with 11 samples, effectively the maximum).

| Workload | Baseline p50 | Candidate p50 | Baseline p95 | Candidate p95 | Ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| HashMap insert | 105.42 | 136.67 | 106.13 | 159.60 | 1.30 |
| HashMap hit | 17.11 | 24.05 | 17.15 | 24.49 | 1.41 |
| HashMap miss | 16.04 | 16.38 | 16.27 | 16.45 | 1.02 |
| HashMap churn | 56.08 | 78.36 | 56.57 | 78.84 | 1.40 |
| AHashMap insert | 45.78 | 79.25 | 46.43 | 81.29 | 1.73 |
| AHashMap hit | 8.76 | 22.67 | 8.79 | 22.72 | 2.59 |
| AHashMap miss | 12.30 | 16.62 | 12.34 | 16.99 | 1.35 |
| AHashMap churn | 22.43 | 59.14 | 23.15 | 59.77 | 2.64 |
| HashSet insert | 52.85 | 85.85 | 53.77 | 86.39 | 1.62 |
| HashSet hit | 14.74 | 22.72 | 14.79 | 22.88 | 1.54 |
| HashSet churn | 40.39 | 63.16 | 41.02 | 63.60 | 1.56 |
| Vector append | 4.61 | 5.20 | 4.85 | 6.15 | 1.13 |
| Vector copy/detach, 4,096 elements | 396.19 | 337.60 | 404.10 | 363.57 | 0.85 |
| Vector unvalidated sort, 4,096 elements | 162,220.31 | 153,646.09 | 164,726.56 | 155,367.97 | 0.95 |
| String copy/detach, 16 characters | 56.89 | 51.26 | 57.94 | 52.10 | 0.90 |
| AHashMap String hit, 16 characters | 51.40 | 42.83 | 53.39 | 43.13 | 0.83 |
| String copy/detach, 128 characters | 59.98 | 60.00 | 61.39 | 61.21 | 1.00 |
| AHashMap String hit, 128 characters | 182.54 | 111.56 | 182.99 | 112.20 | 0.61 |
| Hash 8 bytes | 4.81 | 4.06 | 4.86 | 4.31 | 0.85 |
| Hash 32 bytes | 10.83 | 3.70 | 10.86 | 3.80 | 0.34 |
| Hash 128 bytes | 37.43 | 7.08 | 37.82 | 7.16 | 0.19 |
| Hash 256 bytes | 76.74 | 38.53 | 77.20 | 39.10 | 0.50 |
| Hash 4,096 bytes | 1,226.46 | 167.38 | 1,235.79 | 168.36 | 0.14 |
| Hash 65,536 bytes | 19,743.19 | 2,290.84 | 21,475.90 | 2,483.28 | 0.12 |

Candidate 8-byte hashing had 6.02% MAD; do not overstate that small-key gain.
Vector detach MAD was 2.00% baseline / 2.31% candidate. The major integer map/set
regressions are much larger than their measured spread.

# Live allocation bytes

Godot-requested live bytes at the filled container, **not** allocator overhead,
peak usage or RSS. Integer input fixtures are outside Godot accounting. String
map numbers exclude prepared key buffers; detach numbers include both source
and detached buffers. Sizes repeated identically across all samples.

| Workload/container | Baseline bytes | Candidate bytes | Change |
| --- | ---: | ---: | ---: |
| HashMap integer keys | 1,376,316 | 2,165,135 | +57.3% |
| AHashMap integer keys | 917,504 | 1,378,623 | +50.3% |
| HashSet integer keys | 786,512 | 1,182,075 | +50.3% |
| AHashMap String keys | 163,840 | 223,551 | +36.4% |
| Vector append | 163,880 | 163,880 | unchanged |
| Vector detach | 32,816 | 32,816 | unchanged |
| Vector sort | 16,408 | 16,408 | unchanged |
| String detach, 16 characters | 184 | 184 | unchanged |
| String detach, 128 characters | 1,080 | 1,080 | unchanged |

# Verification and limits

- All 24 workloads passed fail-fast checks, cross-variant container checksums,
  repeatability and allocation cleanup. Byte-hash checksums intentionally differ.
- Isolated production editor with `tests=yes` built successfully after isolating
  upstream wyhash's non-inline prime helpers in the reference-test namespace.
  This test-only fix did not change the benchmarked production files.
- New migration tests: **9 passed, 3,749,785 assertions**.
- Focused Vector/String/HashMap/AHashMap/HashSet/Array/Dictionary/Variant selection:
  **269 passed, 3,801,958 assertions**, including the migration tests.
- No whole-engine/frame-time benchmark, full test-suite run, rendering smoke,
  .NET runtime, sanitizer or cross-platform validation was performed.
- The extra head table plus dense collision-chain entries are a plausible cause
  of increased memory and lookup work, but no ablation/profile isolates their
  contribution. Do not assign all regressions to robin-hood hashing itself.

Raw samples, source manifest, build logs and JSON/CSV summaries remain in the
ignored `devtools/out/core_templates_bench/` directory. Reproduce with the
commands in [developer tools](../../devtools/README.md).[^boundary]
