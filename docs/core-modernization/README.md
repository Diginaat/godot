# Godot core modernization

Performance and compatibility project; not a wholesale STL/SIMD rewrite.

**Resume:** read [memory.md](memory.md), then the active phase. Exact original
baseline: `172e099b95307708005dafcf719693d20b1e7088`. Feature branch:
`feat/core-stl-simd-modernization`, created from `dev-stl-core` with existing
uncommitted work preserved. No push authorized by this checkpoint.

| Phase | State | Record |
| --- | --- | --- |
| 0 Audit/baseline | Initial checkpoint complete; coverage expansion remains | [00](phases/00-audit.md) |
| 1 Containers | LocalVector bulk copy kept; `RID_Alloc::owns()` lock-free on x86-64 (D012, scene instancing -11%) | [01](phases/01-containers.md) |
| 2 Hashing | String-key byte hash kept; `HashSet` one allocation per table (D013, scene instancing -7%); backend selection open | [02](phases/02-hashing.md) |
| 3 Strings | Preliminary copy/key-hash results only | [03](phases/03-strings.md) |
| 4 x86 SIMD | Packed Vector3 dot/cross rejected; `DynamicBVH` volumes in SSE (D014, node churn -2.6%) | [04](phases/04-x86-simd.md) |
| 5 ARM NEON | Blocked until x86 acceptance | [05](phases/05-arm-neon.md) |
| 6 Integration | Not started; no production acceptance | [compatibility](compatibility.md) |

References: [architecture](architecture.md), [compatibility](compatibility.md),
[benchmarks](benchmarks.md), [decisions](decisions.md).

Engine builds remain SCons-based. Standalone benchmark drivers are measurement
tools, not an alternative engine build system. Integrate accepted code/tests
through existing SCons source lists and architecture configuration.

Each implementation checkpoint needs isolated diffs, correctness tests, baseline
comparison, compatibility review and a scoped commit. Never stage the inherited
working tree wholesale. Keep containers, hashing, strings and SIMD commits separate.
