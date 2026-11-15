# Architecture audit

## Evidence boundary

Read source, not just the knowledge graph: graph reports an older HEAD and
unresolved relationships. This is initial structural localization, not a complete
call-site/lifetime audit or a profile of every engine workload.

| Layer | Sources | Required properties / opportunities |
| --- | --- | --- |
| Allocation | `core/os/memory.h`, `.cpp`, `safe_refcount.h` | Godot accounting, header alignment, placement lifecycle, OOM behavior |
| Shared arrays | `cowdata.h`, `vector.h` (a packed `flat_shared_buffer.h` replacement was tried and dropped) | COW, contiguous pointers, signed sizes, explicit initialized/uninitialized resize, one-pointer storage handle |
| Unique arrays | `local_vector.h`, `fixed_vector.h`, `paged_array.h` | Reuse existing unique-ownership storage before introducing another abstraction; candidate for STL/allocator evaluation |
| Stable-node containers | `list.h`, `hash_map.h`, `rb_map.h`, `rb_set.h` | Stable identity; HashMap insertion order; ordered containers require comparator-only keys |
| Packed hash containers | `a_hash_map.h`, `hash_set.h` | Dense iteration, indices stable until removal, swap-last erase |
| Hash policy | `hashfuncs.h`, `.cpp`, custom type hashers | Public default hashes feed API fingerprints, RID/ObjectID, NodePath; custom equality and NaNs matter |
| Strings | `core/string/ustring.h`, `.cpp`, `string_name.*` | UTF-32 String, encoded CharString variants, terminators, COW, conversion and interning boundaries |
| Value math | `core/math/vector*.h`, `basis.h`, `transform*.h`, `quaternion.h` | Compact public layouts, scalar constexpr paths, float/double configurations; private batch layouts are separate |
| Extension boundary | `core/extension/extension_api_dump.cpp`, Mono runtime interop | Explicit type sizes/offsets, native/managed layout agreement |

Godot container elements assume byte relocatability in places; STL element move
requirements are not equivalent. Likewise, std::vector value initialization is
not Godot's uninitialized resize, and deep-copy storage is not COW.

## Existing unaccepted prototype

The working tree already replaced CowData with packed COW storage and STL
algorithms, and introduced a robin-hood full-hash-to-head index plus dense
collision chains. That preserves important semantics but adds metadata and
lookup work. It is not direct std::vector storage, and its numeric-container
regressions prevent acceptance. Do not confuse its tests with baseline tests.

## Highest-value next investigations

1. Remove redundant indexing/hashing cost in an isolated experiment, not through
   an unreviewable global replacement. Compare original Godot, direct library
   backends and the full Godot adapter under matched hashes and semantics.
2. Evaluate uniquely owned storage before disturbing shared String/PackedArray
   ABI. Measure construction, relocation, allocation count and iteration.
3. Isolate byte-hash improvements from container layout changes. Measure both
   algorithms at identical lengths before choosing the dispatch boundary.
4. Profile native math call sites before changing value types. Existing packed
   dot/cross kernels beat the tested intrinsics; persistent batch SoA is promising
   only where conversion costs amortize and ownership fits the consumer.

These are priorities, not measured end-to-end speedup predictions.
