---
type: Mistake
title: Benchmark infrastructure depended on dirty prototype files
description: Original-source isolation caught a generated-header prerequisite and unconditional experimental dependency fingerprints before the phase-zero commit.
tags: [core, benchmarks, build, isolation]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
---

The Phase 0 standalone control initially failed outside the dirty working tree:
the real Memory translation unit needs the SCons-generated profiling header,
and the source manifest unconditionally expected prototype headers/vendor files.
The driver now reports the SCons prerequisite explicitly, fingerprints it,
detects hash-policy availability per source variant and fingerprints optional
prototype dependencies only when present.

Both original-source control binaries then compiled and passed all 24 workloads.
The math probe also compiled and passed against that original-source snapshot.
No experimental engine implementation was needed for Phase 0 infrastructure.

Rule: validate an infrastructure-only commit against the actual baseline plus
explicit generated prerequisites; a dirty-tree success can hide uncommitted
implementation dependencies. Do not repair isolation by silently importing the
entire dirty prototype or substituting measured code.
