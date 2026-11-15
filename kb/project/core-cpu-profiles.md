---
type: Finding
title: Where engine CPU time goes in core code (sampling profiles)
description: Sampling profiles of five workloads: hash tables are 3-16% of the busiest thread, mostly lookups; scene instancing spends 21% in RID allocator checks, locks and refcounts and 14% in heap calls; container internals are not the main cost, their callers' patterns are.
tags: [core, profiling, containers, hashing, rid, performance]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-16T00:30:00+01:00 }
sources:
  - id: sampler
    resource: ../../devtools/profile/README.md
    title: User-mode sampling profiler and analysis scripts
---

# Conditions

Windows 11, Ryzen 5 3600, MSVC production editor build with
`debug_symbols=yes` (same optimization), branch
`feat/core-stl-simd-modernization` after merging `dev-gpu-renderer`
(`640dae3f85`). `devtools/profile/sampler.exe`, 1-2 ms interval, 12-25 s per
workload, innermost inline frame. Percentages are of the busiest thread's
busy samples (waits excluded). Editor binaries count allocations
(`DEBUG_ENABLED`); release templates do not.

# Core share by workload

| Workload | core/templates + string + math | Hash tables | Other core items |
| --- | --- | --- | --- |
| `nodes`: 10k moving nodes, 200 freed and added per frame | 36.5% | 4.2% | LocalVector 7.0% (`Node::_update_children_cache_impl` walks the children), DynamicBVH 6.0%, `Span::find` 2.0% (`SceneTree::add_to_group`) |
| `script`: GDScript logic | 28.4% | 6.3% (Dictionary's `HashMap<Variant, Variant>` 3.5%) | String 9.4% (`sprintf` 3.6%), CowData 7.3%, refcounts 2.6% |
| `instance`: 2k-node scene instanced and freed per frame | 37.3% | 15.6% | RID_Alloc 4.8%, StringName 3.4%, refcounts 3.2% |
| Editor startup, test project | 30.1% | 10.9% (mostly Theme lookups) | StringName 4.2%, CowData 4.2% |
| Overgrown Subway level, render thread | 22.9% | 3.0% | DynamicBVH 4.5%, RID_Alloc 2.9% |

Hash table time is almost all lookups (`HashMap::_lookup_idx_unchecked`
3.4-5.2%, `HashSet::_lookup_key_idx` 2.5-3.2%, `AHashMap::_lookup_idx`
0.5-2.5%), which are memory-latency bound: the hash array, then the element
node. On `instance` their callers are signals (`emit_signalp`, `connect`,
`_disconnect`, `is_connected`: 5.1%) and the renderer's `DependencyTracker`
(5.2%).

# Scene instancing: RIDs, locks, heap

With stack samples (`instance`, 4,578 busy samples, waits excluded):

- RID allocator checks, mutex locks and atomics: 12.6% in `RID_Alloc` and
  SRW lock acquire/release. Most comes from `RenderingServer::free()`, which
  asks about twenty thread-safe `RID_Owner`s in turn whether they own the
  RID (`owns_mesh` 2.3%, `owns_shadow_atlas`, `owns_texture`,
  `owns_reflection_probe`, `owns_shader`, ...), each `owns()` taking the
  owner's mutex, and from `RendererSceneCull::free` and
  `RendererCanvasCull::free`. `get_or_null()` is already lock-free.
- Heap: `RtlAllocateHeap` 9.9% and `RtlFreeHeap` 4.0%. Largest callers:
  `DependencyTracker::update_dependency` and `update_end` and its destructor
  (about 4.7%, node allocations of `HashMap`/`HashSet` inserts), signal
  `connect`/`_disconnect` (about 2.5%).

Upstream made `owns()` lock-free and reverted it (godotengine/godot
PR #112657, revert #114963): GPUParticles3D broke on Apple Silicon only
(issue #114900); acquire/release fixes and forced fences did not help on an
M4, no root cause was found, nobody reproduced it on x86. This fork does
not target macOS. D012 ([decisions](../../docs/core-modernization/decisions.md))
makes `owns()` lock-free on x86-64 only: the `instance` workload went from
13.83 to 12.37 ms per frame (median of 5 alternating rounds, every round
lower), RID checks and locks from 12.6% to 7.0% of its main thread.

# What it means

The container implementations are not the main cost; how they are used is
(linear scans in scene code, per-insert node allocation, an ownership chain
that locks twenty mutexes per free). Gains are in those call patterns and in
lock-free reads, not in replacing the containers.
