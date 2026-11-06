---
type: Finding
title: Native indirect geometry integration foundation
description: Existing MultiMesh indirect submission is a useful native seam, but not an implemented GPU visibility system.
tags: [rendering, indirect, geometry, architecture]
status: draft
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Findings

The fork's selected Forward+ renderer inherits its normal raster path. Native
MultiMesh storage already exposed transform and indirect-command RIDs, but at
the audited baseline ordinary vertex submission had no source-ID remap. CPU culling treats a
MultiMesh as one spatial object. Do not describe indirect API availability as an
implemented GPU instance culler.

A focused real-RD reproducer found the CPU `-1` visible-instance sentinel uploaded
as unsigned `4294967295` in both indexed and non-indexed surface commands. The
native setter patch resolves it to allocated capacity at the upload boundary,
without changing public sentinel storage or initially zero GPU-owned counts.
Production editor build passed. Focused Vulkan and D3D12 tests pass command-byte,
bounded test-compute-write and native material pixel assertions on one GPU.
Existing standard smoke suite also passes (PhysX GPU game/editor, Vulkan path
tracer and DDGI interior). No performance improvement claimed.

Culling needs stable source instance identity, conservative deformation
bounds and per-view/pass command lifetime. Never overwrite source transforms or
reuse main-view rejection for shadow views. Keep unsupported material/geometry
and backend cases on conventional paths.

The subsequent [native frustum-culling slice](native-multimesh-culling.md)
implements that limited seam; it is not cluster virtual geometry or streaming.

# Records

Source audit and integration runbooks live in
[knowledge/](../../knowledge/PORTING_STATUS.md), not a second OKF bundle.
See [architecture](../../knowledge/INTEGRATION_ARCHITECTURE.md),
[status](../../knowledge/PORTING_STATUS.md) and
[test evidence](../../knowledge/PERFORMANCE_BENCHMARKS.md).
