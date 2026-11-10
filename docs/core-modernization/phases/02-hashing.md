# Phase 2 — hashing (planned)

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
