---
type: Mistake
title: Shared texture mip readback inspected the wrong subresource
description: HZB shader output was correct, but reading a shared mip view returned base-level values rather than the intended parent mip.
tags: [gpu, testing, textures, hzb]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Observed failure

The HZB test read each shared mip RID with texture_get_data. Mip zero matched;
all later levels returned base-region values even though actual shader occlusion
and one-pixel-hole recovery behaved correctly. RD's texture_get_data copy path
enumerates subresources starting at mip zero and does not apply the view's
shared mip offset in that path.

# Correct proof

Read the owning full mip-chain texture once, then slice tightly packed mip
payloads by their known dimensions. Every GPU level then matched the CPU padded
min-depth reduction exactly. No production texture/readback implementation was
changed to hide the test failure.

# Rule

Verify the readback's subresource contract independently of shader sampling.
When checking mip hierarchies, prefer parent-chain readback plus explicit offsets.
