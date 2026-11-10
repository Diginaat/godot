# Phase 0 — audit and baseline

## Scope

Initial structural audit, explicit compatibility/performance risks, dedicated
branch, benchmark infrastructure and restartable plan. Not a claim that all
call sites or the requested benchmark matrix have been exhaustively audited.

Baseline `172e099b95307708005dafcf719693d20b1e7088`. Existing dirty prototype is
retained outside this checkpoint. Graph inspection preceded source reads, but
its stale HEAD means source evidence takes precedence.

## Delivered

- Required documentation hierarchy, decisions, compatibility matrix and phase gates.
- Reusable container/hash comparison driver: reads baseline via git show,
  preserves working checkout, source fingerprints, checksums/cleanup, alternating
  samples, medians/p95/MAD, live allocation bytes, active-build timing guard.
- Exploratory x86 dot/cross/SoA measurement with SSE4.2 CPU check, no unsafe
  aligned read of public Vector3, conversion costs measured separately.
- User-supplied 2019 benchmark methodology recorded; unordered_dense stability,
  allocator and deletion tradeoffs reviewed from its README. No new backend
  chosen from external rankings.

## Checkpoint verification

Passed standalone isolation check: archived the original HEAD's core/platform
headers and required unchanged thirdparty support, overlaid only the four Phase 0
benchmark files, and supplied the configured SCons-generated empty profiling
header. Both original-baseline and unchanged-source control binaries compile and
pass all 24 checksum/allocation-cleanup workloads without experimental engine
headers or new vendor files. The runner now detects the hash policy per variant
and only fingerprints optional dependencies actually present.

The same source snapshot compiled and ran the math probe: 77 timing rows and
81,920 finite-component comparisons passed. Dot 0.869 vs 1.228 ns/vector and
cross 1.223 vs 1.720 reproduce the prior negative SSE result. No engine math
implementation changed. Python syntax and whitespace checks passed.

Existing prototype editor/test outcomes are prior evidence, not this snapshot's
engine acceptance. Fresh full baseline engine compilation and broader platform/
workload gates were not run in this Phase 0 checkpoint.

## Risks / next

Initial benchmarks omit allocation counts, multiple dataset sizes, varied hit
rates, iteration and representative engine workloads. Linux/ARM unavailable to
this initial Windows run. Expand coverage before accepting a modernization.
Proceed to Phase 1's narrow unique-storage evaluation, not a wholesale rewrite.
