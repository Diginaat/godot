---
type: Finding
title: Opt-in native scene-import cluster cooking
description: Cook cluster data from final source arrays before embedded or external save, retaining shared meshes and conventional shadow resources.
tags: [geometry, clod, import, resources]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Native seam

ResourceImporterScene's `meshes/generate_cluster_lods` defaults false. Conversion
cooks after lightmap unwrapping, ordinary LOD generation and index optimization,
before either embedded or external resource save. Shared ImporterMesh conversion
is cached, so each shared mesh cooks once. Unsupported morphs, skinning, custom
channels and unavailable meshoptimizer emit a warning and keep source geometry.

Runtime resident CLOD additionally needs its opt-in project setting. Separate
optimized shadow meshes remain valid: main depth binds source vertex arrays for
source hierarchy indices; shadow passes keep their independently numbered mesh.
The raster test reverses shadow vertex IDs while preserving geometry to catch
wrong source/shadow-buffer pairing.

# Proof

Real editor import of three inline-data glTF fixtures: enabled cache is saved and
matches reloaded source; disabled cache remains absent; unsupported morph import
retains its blend shape and conventional mesh, with expected warning. Two nodes
retain shared ArrayMesh identity and optimized shadows. Headless cache inspection
and Vulkan/D3D12 six-part matrices pass. Production build and focused class-help
checks and final standard smoke rerun pass. External-save branch is wired
but not independently exercised by the embedded glTF fixtures.

Expanded suite exceeded its first 120-second outer tool budget; targeted headless
and Vulkan import checks passed, then both complete suites passed with 300-second
budget. A tool timeout alone is not evidence of an import deadlock.

Evidence: `devtools/test/large_world.ps1`, `import_clusters.gd`,
`import_fixtures/`, native `clod_raster.gd` and `resource_importer_scene.cpp`.
