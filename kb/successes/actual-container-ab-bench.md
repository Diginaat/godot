---
type: Success
title: Benchmark actual container variants without switching the checkout
description: Isolated HEAD snapshots, verbatim String methods and live allocation accounting exposed compatibility-wrapper regressions before publication.
tags: [core, benchmarks, testing, containers]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T18:09:00Z }
---

# What worked

`devtools/test/core_templates_bench.py` snapshots only the relevant HEAD headers
and source files with `git show`, then compiles baseline and candidate with the
same MSVC flags. Actual Godot allocation/hash implementations remain in use;
the two required String method definitions are extracted verbatim to avoid
linking unrelated OS/Variant services. Only diagnostics are replaced with
fail-fast handlers. No checkout switch, reset or user-file removal is needed.

Checksum equivalence, allocation cleanup and repeatable operation counts guard
the workload. An active-build guard rejected contended timings. Eleven
alternating pairs plus recorded p50/p95/MAD revealed substantial numeric-container
regressions and higher live allocation bytes despite faster byte hashing.
See [full measurements](/project/core-template-benchmarks.md).

# Reuse

Use before publishing a foundational container/backend change. Measure both
lookup/churn and live allocation bytes; a fast hashing primitive does not prove
that an API-compatible wrapper is a faster or smaller container. Keep synthetic
microbenchmark results separate from whole-engine frame-time claims.
