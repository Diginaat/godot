---
type: Mistake
title: Assumed bundled xxHash enabled XXH3
description: Zstandard's local xxHash header disables XXH3 despite containing its source.
tags: [core, hashing, thirdparty, build]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
---

# What happened

The first container-hashing compile attempted to reuse
`thirdparty/zstd/common/xxhash.h` with `XXH_INLINE_ALL`. MSVC rejected the XXH3
call: the header unconditionally defines `XXH_NO_XXH3` in its Zstandard-local
adaptation. The presence of XXH3 declarations in the file was not evidence that
they were enabled. A separate pinned upstream xxHash header fixes the dependency
without changing Zstandard's configuration.

The same focused compile caught an incorrect hand-calculated constexpr mixer
assertion: for `(UINT64_MAX * UINT64_MAX)`, the low half is 1 and high half is
`UINT64_MAX - 1`; their XOR is `UINT64_MAX`. Regression tests compare the portable
integer mixer against upstream `wyhash64`, rather than relying only on hand math.

The full editor link then found another header-integration issue: upstream
wyhash also defines non-inline `is_prime` and `sprp` helpers. Including a
reference copy in the test TU duplicated the production definitions. The
reference header now lives in a test-only namespace; benchmarked production
code is unchanged. The editor link and focused regression tests then passed.

# Rule

Inspect thirdparty-local configuration at the top of a reused header. Compile
changed hashing code and check known answers before committing a long rebuild.
Also check the final link: a header advertised as header-only can still contain
externally visible definitions. Isolate test-reference copies when necessary.
