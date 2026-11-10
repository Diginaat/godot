# Persistent project memory

## Identity / provenance

- Project: Godot core modernization; canonical entry [README](README.md).
- Baseline: `172e099b95307708005dafcf719693d20b1e7088`.
- Branch: `feat/core-stl-simd-modernization`, created from `dev-stl-core`.
- Date: 2026-11-10. Writer: `opencode/gpt-6.1-sol`.
- Source evidence and research: [architecture](architecture.md),
  [decisions](decisions.md). Quantitative conditions: [benchmarks](benchmarks.md).
- Mirror discovery in the canonical project KB; these files are the compact
  restart/runbook view, not a second independent knowledge-base format.

## Working-tree ownership / checkpoint boundary

The branch started dirty. Do not reset, stash, restore or stage everything.
Inherited uncommitted changes include container/hash/String/Mono experiments,
vendor libraries and license updates, regression tests and their KB records.
Other pre-existing changes include agent/config files and glTF import fixtures.
Phase 0 commits only this documentation, reusable benchmark infrastructure and
the Phase 0 KB entry/index/log additions. No engine implementation accepted or
committed. Branch creation preserved every existing change.

## Current state

- Phase 0 committed as `ad1573b0e4`; Phase 1 infrastructure `ce25718574`.
  First LocalVector copy slice passes the Windows build/test gate; Phase 1 remains open.
- Existing COW/STL-algorithm and robin-hood adapter prototype: builds and passes
  focused correctness, but **fails numeric-container performance/memory goals**.
- No engine math modification. Explicit SSE dot/cross candidates lost locally.
- Production engine uses SCons; C++17. Local configured SDKs enable PhysX GPU,
  Blast and Flow; machine-specific SDK paths must not enter public docs.
- Current Windows build interpreter: `py -3.14 -m SCons`; default `python` did
  not have SCons. Use `-j10`, consistent `production=yes`, isolated output suffix.

## Validation ledger

| Evidence | State / limitation |
| --- | --- |
| Existing prototype isolated editor | Passed MSVC production build with tests=yes |
| Focused core tests on prototype | 269 passed / 3,801,958 assertions, includes nine migration cases |
| Original HEAD standalone benchmark | Built; correctness and cleanup passed |
| Candidate standalone benchmark | Built; checks passed, but numeric map/set regressions |
| Exploratory x86 math | Built; finite float32 checks passed; no accepted SIMD implementation |
| Phase 0 isolated original-source snapshot | Both container control binaries compile and pass all 24 workloads; math probe compiles and passes finite checks; no experimental engine headers/vendor files needed |
| Phase 1 unique array baseline | 56 LocalVector/STL paired workloads, seven sizes; count/timing checksum parity and initial contracts pass |
| Phase 1 copy candidate | 11 alternating original/candidate process pairs; bulk-copy improvement at 256/4K ints; allocations unchanged; small-copy costs and control confounders documented |
| Phase 1 editor build/test gate | Passed MSVC production editor build (15m09s); 279 focused tests / 3,803,093 assertions, zero failures; dirty-tree prototype dependencies remain outside this slice |
| Full suite, scene/renderer/physics/C# smoke | Not run in this audit |
| GCC/Clang/Linux, double precision, sanitizers, ARM | Pending |

## Decisions / unresolved issues

Public layouts, COW and hashes are compatibility constraints. Do not pad vectors,
delete needed shared storage or use unconditional C++20 features. The current
hash adapter adds a head table plus collision metadata; memory/time regressions
are real, but causal attribution needs isolated comparisons. 256-byte hash
dispatch is provisional. std::vector storage and unordered_dense remain
unaccepted candidates. Do not port rejected SSE kernels to NEON.
Latest user direction: an engine-owned hashmap is the primary Phase 2 candidate;
existing Godot tables are the baseline and reusable foundation. Thirdparty maps
remain comparison controls, not mandatory dependencies. See D011.

## Next independent task

Next Phase 1 slice: threshold-neighborhood copy benchmarks (255/256/257), more
element types, then remaining compiler/sanitizer/contract coverage. Current
LocalVector optimization preserves layout/storage and uses bulk copy only for
>=256 trivially copyable elements; tests cover nontrivial/no-default construction,
empty spans, independence and the bulk path. Do not modify source during a build.
No std::vector storage replacement accepted. Keep inherited shared-container/
hash/String/Mono prototypes and unrelated work unstaged. Evidence and caveats:
kb/project/unique-array-evaluation.md. Phase 2 native-map work follows its gates.

## Resume commands

1. `git status --short --branch` and `git log -3 --oneline`; identify staged and
   unstaged ownership before editing.
2. Read phase [00](phases/00-audit.md), then [01](phases/01-containers.md).
3. Run still-current focused proof; do not assume earlier artifacts match HEAD.
4. Stage named phase files only; inspect `git diff --cached --stat` and diff,
   test an isolated committed/index snapshot where dirty implementation work
   could mask dependencies. No publication without full acceptance.
