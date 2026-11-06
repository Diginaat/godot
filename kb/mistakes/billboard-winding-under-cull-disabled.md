---
type: Mistake
title: Camera-facing splats were wound as back faces
description: Voxel splat quads were counter-clockwise from the camera; with cull_disabled Godot flips NORMAL on back faces, so every splat lost direct light and rendered about 6x darker.
tags: [rendering, shaders, voxel-proxy, testing]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T12:00:00+01:00 }
---

# Observed failure

The voxel proxy fixture rendered a MultiMesh grove and its splat proxy from the
same distant camera. Coverage matched, but the proxy's mean linear colour was
about 0.165 times the source on all three channels, on Vulkan and D3D12.

# Cause

The splat vertex shader expands each quad along the camera's right and up
axes, and the index order (0, 1, 2, 0, 2, 3) over corners (-1,-1), (1,-1),
(1,1), (-1,1) is counter-clockwise as seen from the camera. Godot's front faces
are clockwise. The material uses `cull_disabled`, so the quads still drew, but
the scene shader flips `NORMAL` for back faces: every splat normal pointed away
from the sun and only ambient light remained. A uniform per-channel ratio is
the signature of a lighting error rather than a colour-space error (sRGB
conversion would change channels by different factors).

# Rule

For generated camera-facing geometry, wind triangles clockwise as seen from
the camera, and check it with a lit render rather than only with unlit or
geometric assertions. `cull_disabled` hides winding mistakes from coverage
tests; only lighting reveals them.
