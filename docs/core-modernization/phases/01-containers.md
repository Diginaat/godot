# Phase 1 — containers (not accepted)

Inherited prototype uses packed COW storage plus STL algorithms, not std::vector
storage. Preserve it unstaged; numeric hash regressions are tracked separately.

Next slice: baseline-only lifetime/COW/aliasing/initialization/error/allocator
contract tests and N=0/1/16/256/4K/32K/1M operation/allocation-count matrix.
Evaluate one uniquely owned array path using Godot-compatible std::vector
allocation. Prove pointer, element and failure semantics before adoption.

Accept only a reproducible benefit with no relevant regression. Keep shared
Vector/PackedArray/String layouts unchanged until compatible storage is proven.
Do not remove CowData merely because it is an old implementation.

Gate: isolated build, focused lifecycle tests, relevant sanitizer/configuration
coverage, baseline comparison and scoped commit. No accepted implementation yet.
