# Decisions

| ID | Decision | Reason / evidence |
| --- | --- | --- |
| D001 | Performance acceptance requires local evidence, not a library name | Existing adapter slows integer AHashMap hits 2.59x and adds ~50% live bytes |
| D002 | Preserve public math layout; optimize private working representations | Vector2/3 and extension size assertions; padding changes stride/ABI |
| D003 | Preserve COW where required; std::vector is a candidate, not a mandate | Shared String/PackedArray handles and uninitialized resize are not interchangeable with STL defaults |
| D004 | Separate container, hashing, strings and SIMD commits | Independently reviewable semantics/performance and manageable rollback |
| D005 | Keep C++17; no unconditional consteval/constinit | Actual SConstruct standard |
| D006 | Evaluate maintained unordered_dense alongside robin_hood and original Godot | Dense iteration/allocator support are useful; reference stability and deletion costs differ |
| D007 | x86 before NEON; no mechanical intrinsic translation | Equivalent semantics and architecture-specific measured implementation required |
| D008 | Keep legacy public hashes; benchmark internal hash dispatch separately | Public hashers feed fingerprints and type hashes beyond tables |
| D009 | Do not accept a 256-byte boundary as optimal yet | Existing byte tests compare legacy/new dispatch, not wyhash versus XXH3 at equal lengths |
| D010 | Freeze implementation files during builds | Prior parallel build saw mixed live-header edits; results require matching source snapshots |
| D011 | Godot-native hashmap is the primary implementation candidate | User requested an engine-owned hashmap; improve/reuse existing native tables where justified, compare libraries as controls, avoid a second full lookup index |

## Research provenance

- [Hashmaps Benchmarks — Overview (2019)](https://martin.ankerl.com/2019/04/01/hashmap-benchmarks-01-overview/), supplied by the user:
  20 maps × 5 hashers × 10 workloads, nine runs and medians; isolated core,
  fixed frequency, separate processes, GCC 8.2/Linux and native ISA flags.
  Adopt the factorial map/hasher and workload methodology, not its ranking as a
  prediction for Godot's MSVC build or API adapters. Current local benchmarks
  do not have its CPU isolation/frequency controls.
- [robin-hood-hashing README](https://github.com/martinus/robin-hood-hashing):
  general development ended; flat/node reference guarantees differ. Existing
  uncommitted prototype vendors MIT 3.11.5. No new dependency acceptance here.
- [unordered_dense README](https://github.com/martinus/unordered_dense), accessed
  2026-11-10: C++17+, MIT, dense vector storage, allocator support; ordinary
  references/iterators invalidate on mutation, segmented variants have different
  guarantees, deletion has extra lookup cost. Pin an immutable revision before
  reproducible evaluation; main-branch documentation is not a measured result.
- [wyhash](https://github.com/wangyi-fudan/wyhash), Unlicense, and
  [xxHash](https://github.com/Cyan4973/xxHash), BSD-2-Clause: existing local byte
  tests support further evaluation, not replacing every engine hash.
