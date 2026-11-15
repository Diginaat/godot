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

- 2026-11-16: resumed on user direction (continue this branch, not the
  literal brief; macOS is not a target). Merged `dev-gpu-renderer`
  (`640dae3f85`); worktree `GodotPhysxDev-core` next to the main checkout,
  built with `production=yes tests=yes debug_symbols=yes`.
  Sampling profiles of five workloads (`devtools/profile/`,
  [findings](../../kb/project/core-cpu-profiles.md)) gave the frame-time
  reason asked for below: container internals are not the main cost, their
  callers' patterns are. D012: lock-free `RID_Alloc::owns()` on x86-64,
  scene instancing 13.83 to 12.37 ms per frame. D013: one-block `HashSet`
  and a `LocalVector` in `DependencyTracker::update_end()`, 12.58 to 11.69
  ms (node churn 14.84 to 14.59). D014: `DynamicBVH` volumes in SSE,
  node churn -2.6% against a scalar control build (code layout alone moves
  a build by about 1.5%; compare against a control of the same source).
- Next candidates, by measured share on scene instancing (after D013): heap
  calls 14.6%, largest `Object::connect` 2.45% and `_disconnect` 0.77%
  (signal connections), `DependencyTracker::update_dependency` 1.48%
  (the HashMap node of `Dependency::instances`; an `AHashMap` there is not
  safe: `deleted_notify()` callbacks erase other trackers while it
  iterates), `ClassDB::creator`; hash lookups from signals and
  dependency tracking; StringName construction 3.4%; on node churn the
  children cache walk (LocalVector 7%) and `SceneTree::add_to_group`'s
  linear `find` (2%); editor startup Theme lookups.
- 2026-11-15: the COW/STL-algorithm storage and robin-hood index prototype was
  dropped (numeric hash tables 1.3-2.6x slower, 50-57% bigger); saved on a
  local backup branch. Kept as patches: LocalVector bulk copy from 256
  elements (`ee5973f764`; the small-copy "regression" was the harness, see
  [unique-array evaluation](../../kb/project/unique-array-evaluation.md)) and
  the wyhash/XXH3 byte hash for String/CharString table keys
  ([numbers](../../kb/project/core-template-benchmarks.md)).
- Next core work needs a frame-time reason first (2026-11-15); the profiles
  of 2026-11-16 provide one per candidate above.
- Engine math: only `DynamicBVH` (D014). Explicit SSE dot/cross on public
  packed `Vector3` lost locally (conversion cost); keep SIMD to private
  resident layouts.
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
| Phase 1 boundary follow-up | 12 sizes / 11 alternating pairs, checksum and allocation parity pass; small-copy regressions block performance acceptance; tools-only change |
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

Next Phase 1 slice: investigate small-copy codegen/layout and representative
callers. Boundary sweep is complete, not pending. It finds small-copy regressions
despite passing correctness/allocation gates; inspect before retuning or explicit
rollback. Then extend element types and compiler/sanitizer/contract coverage. Current
LocalVector optimization preserves layout/storage and uses bulk copy only for
>=256 trivially copyable elements; tests cover nontrivial/no-default construction,
empty spans, independence and the bulk path. Do not modify source during a build.
No std::vector storage replacement accepted. Keep inherited shared-container/
hash/String/Mono prototypes and unrelated work unstaged. Evidence and caveats:
kb/project/unique-array-evaluation.md. Phase 2 native-map work follows its gates.
Boundary artifacts: devtools/out/unique_array_bench/boundary/copy/. Earlier
seven-size results remain in the separate copy/ directory. --boundary is copy-
only; non-power-of-two sizes must not enter the random-access permutation suite.

## Resume commands

1. `git status --short --branch` and `git log -3 --oneline`; identify staged and
   unstaged ownership before editing.
2. Read phase [00](phases/00-audit.md), then [01](phases/01-containers.md).
3. Run still-current focused proof; do not assume earlier artifacts match HEAD.
4. Stage named phase files only; inspect `git diff --cached --stat` and diff,
   test an isolated committed/index snapshot where dirty implementation work
   could mask dependencies. No publication without full acceptance.
