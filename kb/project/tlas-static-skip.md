---
type: Decision
title: Keep the TLAS in frames where nothing changed
description: RenderRaytracing rebuilt the TLAS every frame. It now skips the build when the instance list equals the last one built for that viewport and no BLAS was built or refit anywhere since (BLASes are shared). DDGI interior at 1080p, static: acceleration structure stage 0.094 to 0.007 ms. Moving scenes still rebuild every frame.
tags: [raytracing, ddgi, path-tracer, tlas]
status: stable
sources:
  - id: bench
    title: misc/ddgi_test_project --bench with --gpu-profile, view interior, 1920x1080, RTX 5070, 2026-11-27
  - id: nvidia
    title: NVIDIA, Best Practices for Using NVIDIA RTX Ray Tracing (rebuild the TLAS rather than refit it)
    url: https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Decision

`RenderRaytracing::build_acceleration_structures()` (DDGI and the path
tracer) now keeps the TLAS when:

- every instance (BLAS, transform, id, mask, hit group range, flags)
  equals the list the viewport's TLAS was last built from, and
- no BLAS was built or refit since that build. `blas_generation` counts
  every BLAS build and refit. A BLAS is shared by all viewports, so a
  per-viewport dirty list is not enough: another viewport may have
  rebuilt it earlier in the frame.

A new or grown TLAS is always built. Roadmap G8 named a TLAS refit for
frames where only transforms changed; it is not done, because NVIDIA
recommends rebuilding the TLAS (a refit degrades it as objects move),
and the rebuild here costs 0.09 ms.

# Measured

DDGI interior, 1920x1080, RTX 5070, Vulkan, `--bench` with
`--gpu-profile`. The `as` stage runs from "DDGI Build Acceleration
Structures" to the next timestamp:

| Scene | Before | After |
| --- | --- | --- |
| Static (`--anim=0`), `as` | 0.094 ms | 0.007 ms |
| Static, GPU frame | 1.307 ms | 1.227 ms |
| Animated (`--anim=1`), `as` | (built every frame) | 0.094 ms, still built every frame |

These are single runs, so the GPU frame number is only a guide.

# Tests

- Smoke tests pass: path tracer room, DDGI interior, PhysX.
- DDGI interior flicker over three runs: `mean_linear` 0.0123 as before,
  `flicker_std` 0.027-0.033. The spread was the same before the change
  (0.029-0.030).
