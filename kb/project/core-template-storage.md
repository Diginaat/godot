---
type: Decision
title: Packed core containers and compatible hashing
description: Dropped 2026-11-15. Packed shared storage and a robin-hood index for HashMap/AHashMap/HashSet; numeric maps got slower and bigger, so only the String-key byte hash was kept.
tags: [core, containers, stl, hashing, compatibility]
status: deprecated
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
sources:
  - id: storage
    resource: ../../core/templates/flat_shared_buffer.h
    title: Packed shared storage implementation
  - id: index
    resource: ../../core/templates/robin_hood_hash_index.h
    title: Shared robin-hood index
  - id: tests
    resource: ../../tests/core/templates/test_flat_storage.cpp
    title: Migration regression tests
  - id: robin-hood
    url: https://github.com/martinus/robin-hood-hashing/tree/9145f963d80d6a02f0f96a47758050a89184a3ed
    title: robin-hood-hashing 3.11.5
---

# Dropped (2026-11-15)

Not merged. The [A/B benchmark](/project/core-template-benchmarks.md) showed
integer-keyed HashMap/AHashMap/HashSet 1.3-2.6x slower with 50-57% more live
bytes, and the storage rewrite gained only 5-15% on Vector/String copies. The
files below were removed from the branch (kept on a local backup branch,
`backup/core-stl-wip-2026-11-15`). What survived as its own patch:
`hash_buffer()` (wyhash below 256 bytes, XXH3 above) used only for String and
CharString keys inside the three hash tables (`hash_table_key()`), and the
LocalVector bulk copy from 256 elements. A new index design must beat the
current tables on the integer cases before it comes back.


# Compatibility boundary

`Vector`, `String` and character strings retain copy-on-write and their existing
handle sizes. `FlatSharedBuffer` replaces the former storage implementation;
header offsets, Godot allocation accounting, zero-construction traits and raw
pointer APIs remain. Element construction/copy/destruction uses STL algorithms;
byte relocation retains Godot's existing relocatability requirement. Vector uses
STL copy/fill/reverse/sort, retaining Godot's comparator diagnostics when requested.
String's actual header is `core/string/ustring.h`; it uses the packed buffer and
`std::char_traits`. This is **not** a direct `std::vector`/`std::basic_string`
storage substitution, which would change sharing, handle sizes and interop.
C++17 remains the minimum; no unconditional C++20 `consteval` use.[^storage]

`HashMap` keeps its allocator-owned stable nodes and insertion-order list.
`AHashMap` and `HashSet` keep densely packed elements, append-stable indices and
swap-with-last removal. The shared index uses `robin_hood::unordered_flat_map`
over full 32-bit Godot hashes. Packed `{hash,next}` entries resolve equal hashes
with the configured Godot comparator. This avoids duplicated keys and supports
constant custom hashers without the library's limited-probe overflow. All
library allocations use Godot Memory hooks. Public capacity values retain their
existing growth policies; diagnostic indices now enumerate dense entries.
Internal C++ map/set object layouts change and require a complete engine rebuild;
the packed String/Vector extension-boundary layouts do not.[^index]

The index adds bookkeeping. [Controlled microbenchmarks](core-template-benchmarks.md)
show numeric map/set regressions and approximately 50–57% more live allocation
bytes, despite faster byte hashing/String lookup. Do not publish as a performance
upgrade. Upstream robin-hood-hashing no longer receives
ordinary maintenance; 3.11.5 is pinned as explicitly requested.[^robin-hood]

Ordered `RBMap`, `RBSet` and `VSet` retain sorted semantics and comparator-only
key support. They cannot become unordered hash containers without breaking
their API contracts. Consumers such as `LRUCache` inherit the new hash backend.

# Hash policy

Container-internal scalar/pointer hashing uses a portable constexpr wyhash64
mixer. String/C-string byte hashing uses wyhash below 256 bytes and XXH3 at or
above 256 bytes. This boundary is an explicit initial policy, not a measured
crossover. Floating-point zero signs and NaN payloads normalize before hashing.
Custom hashers remain in control of their full hashes. Other self-hashing types
keep their existing `hash()` implementations.

Named DJB2/Murmur functions **and the public `HashMapHasherDefault`** preserve
their outputs: these feed API fingerprints, RID/ObjectID and NodePath hashes,
not only hash tables. `hash_table_key` selects the new policy only inside the
three migrated containers. The separate xxHash copy is required because the
existing Zstandard copy unconditionally disables XXH3.

# Validation state

| Gate | State |
| --- | --- |
| MSVC C++17 syntax check of new regression tests | Passed |
| Standalone constexpr integer mixer against upstream | 1,000,000 value/seed pairs passed |
| Isolated production editor with `tests=yes` | Passed after test-reference symbol isolation |
| Focused Vector/String/hash/Array/Dictionary/Variant tests | 269 passed, 3,801,958 assertions (includes migration tests) |
| New ABI/lifecycle/collision/churn/hash-vector tests | 9 passed, 3,749,785 assertions |
| Full engine tests and runtime smoke | Not run |
| .NET runtime / other platforms / sanitizers | Not run |
| Benchmark harness correctness | HEAD and candidate executables pass checksum/allocation-cleanup checks; contended timings discarded |
| Memory and performance benchmarks | 24 cases, 11 alternating pairs completed; numeric-container regressions block performance acceptance |

Tests cover sharing/detachment, self append and subspans, lifecycle accounting,
UTF-16 traits, stable HashMap nodes across resize/sort/key replacement, 512
constant-hash keys, dense removal, reuse, 10,000 deterministic churn operations
against STL reference state, allocation cleanup and comparison against pinned
upstream wyhash/XXH3 at threshold and alignment edges.[^tests]

## Reproducible microbenchmark

`devtools/test/core_templates_bench.py` reads original headers from `HEAD`
without changing the checkout. Both standalone binaries compile real Godot
allocation/hash implementations and verbatim required String methods. It
compares 24 vector/String/map/set/hash workloads with fixed inputs, matching
non-hash checksums, allocation cleanup, one warmup pair and 11 alternating
sample pairs. Hash algorithms intentionally produce different checksums.
MSVC uses C++17, `/O2 /MT`, no LTO, and `DEBUG_ENABLED` allocation accounting.
It refuses measurement while compilers/linkers or SCons run. See
[developer tools](../../devtools/README.md) for commands and measurement limits.

# Rollback

This changes no serialized format. Roll back the container/hash implementation,
String storage header and Mono offset accessor together, then rebuild the entire
engine; do not mix old and new map/set object files. Keep thirdparty dependency
licensing changes with their dependency files. No user changes, caches or
projects need deletion. Do not publish before validation gates pass.
