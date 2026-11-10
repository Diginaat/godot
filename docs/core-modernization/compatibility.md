# Compatibility and validation gates

Source compatibility, binary compatibility, numerical semantics and serialized
formats are independent. A successful method call or editor build proves none
of the other gates automatically.

| Contract | Gate |
| --- | --- |
| C++ API | Existing signatures, custom allocators/comparators/hashers, incomplete types and element requirements compile unchanged |
| COW/lifetime | Copy/move/self-aliasing, detach, destructor reentrancy, pointer validity, initialization traits and failure paths |
| Native ABI | sizeof/alignof/offsetof, calling convention, Variant storage and PackedArray element stride |
| Managed/extension ABI | GDExtension API dump/layout checks; native/managed offsets and round trips; test real C# and extension binaries |
| Projects/data | Resource, scene and PackedArray round trips; script-visible hashes and deterministic outputs; no silent cache-format change |
| Numerics | NaN/Inf, signed zero, zero normalization, integer overflow/division behavior, operation ordering, float/double |
| Platforms | MSVC/GCC/Clang, Windows/Linux, debug/release, relevant ARM64; mark unavailable execution explicitly |
| Practical performance | Allocation count/live/peak bytes and representative engine workloads, not only microkernels |

Float Vector2/Vector3 are 8/12 bytes; Vector3i is 12 bytes. Their source asserts
packed component layouts. `extension_api_dump.cpp` checks sizes for vectors,
transforms, Basis and other built-ins. Padding public vectors to 16 bytes is not
an ABI-compatible optimization. A 16-byte-aligned array base does not align each
12-byte element, nor permit reading a nonexistent fourth component.

Use private aligned SIMD working data, scalar component reads/register assembly,
and aligned loads only where alignment and readable extent are proven. Never
type-pun scalar storage through a SIMD pointer/union without a valid C++ lifetime
and aliasing model. Double precision needs a distinct internal representation.

SConstruct requires C++17. `consteval` and `constinit` cannot be unconditional;
retain scalar constexpr evaluation and use newer facilities only after explicit
feature checks and a demonstrated need. Do not globally raise the ISA baseline.

Original public `HashMapHasherDefault` and named DJB2/Murmur results remain stable
unless every persisted/public consumer has an explicit compatible transition.
Hash selection depends on immutable key properties, never current table size.

No cross-platform, sanitizer, full-suite, scene/render/physics, C# runtime or
external GDExtension validation is claimed for the inherited prototype.
