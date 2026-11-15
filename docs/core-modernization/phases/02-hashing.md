# Phase 2 — hashing (in progress)

## HashSet: one allocation per table (D013), 2026-11-16

`HashSet` allocated four arrays (hashes, key to hash index, hash to key
index, keys) on its first insert and on every resize, and freed four when
destroyed. They now share one block: the three index arrays, then the keys
at their alignment (keys must not be over-aligned; checked where the block
is allocated, so a member may still name an incomplete key type). Keys move
to the new block by memory copy on resize, which the class already
required. Lookups, inserts, erases and iteration are unchanged.

The renderer's `DependencyTracker` holds a small `HashSet<Dependency *>` per
instance, created and freed with it: its four allocations and frees were
2.7% of the scene instancing workload's main thread
([profiles](../../../kb/project/core-cpu-profiles.md)). The same commit
series moves `DependencyTracker::update_end()` from a `List` (an allocation
per stale dependency) to one `LocalVector`.

| Workload (ms per frame, median of 5 alternating rounds, against D012) | Before | After |
| --- | --- | --- |
| `core_workload.gd --mode=instance` | 12.58 (12.50-12.73) | 11.69 (11.61-11.77) |
| `core_workload.gd --mode=nodes` | 14.84 (14.80-14.98) | 14.59 (14.48-14.66) |

Every round lower on both. Profile after: heap calls of
`DependencyTracker::update_dependency` 2.97% to 1.48%, its destructor out
of the top list, `update_end()` 1.31% to 0.91%. Microbench of large sets
(`core_templates_bench.py`, N=32,768, 11 samples): insert 1.02x, hit
1.03x, churn 0.97x, live bytes equal, so neutral where allocation count
does not matter. Test `[HashSet] Keys of different sizes and alignments
through growth` (1, 8 bytes, Vector3, String through growth, erase, copy,
reset, reserve, move). 1,476 cases pass.

Tried and not kept (2026-11-16): `HashMap`'s two arrays (element pointers
and hashes) in one block, the same way. Against the D013 build, 5
alternating rounds, ms per frame: `instance` 11.12 / 11.14, `nodes` 14.42 /
14.40, `script` 6.82 / 6.89, per-round differences of both signs. Its cost
is the node per element, not the two arrays; reverted to keep `hash_map.h`
as upstream has it. `AHashMap` not tried (lookups dominate it in the
profiles, and its elements grow with `realloc`, which a block would lose).

Existing prototype: faster byte hashes/String keys, slower numeric containers,
higher live allocation bytes. No library adapter accepted as a default.

User direction: generate an engine-owned hashmap. Treat a Godot-native design
as the primary candidate, not another library wrapper. Original Godot tables
already implement robin-hood hashing; reuse their proven machinery where it
fits instead of renaming it or inventing an unnecessary parallel container.
Specify stable-node/insertion-order HashMap and dense AHashMap/HashSet contracts
separately. Keep one authoritative lookup index; collision, deletion and growth
logic must be independently tested. A custom design must beat the original, not
only the rejected adapter. No custom implementation is claimed at this checkpoint.

Compare original Godot, robin_hood flat/node and pinned unordered_dense
normal/segmented variants, using both their defaults and matched hashers.
Measure full adapters preserving Godot ordering, stable identity/dense indices,
custom equality and collision contracts; raw library rankings are insufficient.

Measure wyhash versus XXH3 at equal key lengths and both precisions' float-key
normalization, not table-size-based switching. Preserve public/persisted hashes.
Record maintenance/license/allocator effects and peak/count/live memory costs.

Gate: small/medium/large and collision-heavy tests, mixed hits/churn/iteration,
source/ABI audit and practical workload proof. Commit independently of storage.
