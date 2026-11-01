---
type: Reference
title: Pixel-projected reflections (Cichocki, SIGGRAPH 2017)
description: Planar reflections by scattering: mirror each pixel across the reflector, project it, keep the nearest with an atomic min on a packed offset; hole filling and filtered sampling with anti-bleeding.
tags: [research, reflections, planar, screen-space]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
sources:
  - id: talk
    title: "A. Cichocki, Optimized pixel-projected reflections for planar reflectors, SIGGRAPH 2017 Advances in Real-Time Rendering"
    url: https://www.advances.realtimerendering.com/s2017/PixelProjectedReflectionsAC_v_1.92_withNotes.pdf
---

# Summary

- Reflectors are analytic rectangles given as constants. Projection pass:
  for every pixel, mirror its position across each plane; if the camera
  ray to the mirrored point meets the rectangle (and no other reflector
  is in front), write to the intermediate buffer where it lands.[^talk]
- Several pixels land on the same place: store the screen offset, not the
  color, and keep the smallest with InterlockedMin. The offset is packed
  in one of four axis frames so the component along the dominant axis
  (always positive) sits in the high bits: 12.3 bits along, 12.3 bits
  across (signed), 2 bits frame.
- Reflection pass: decode, fill holes from neighbors (when empty or a
  neighbor's offset is more than 6 px shorter), sample the color filtered
  with the offset's fraction, and pull it toward the point sample in luma
  and chroma separately (YCoCg) against bleeding.
- GTX 1070, 4K, full resolution: 0.95 ms, quality near brute force SSR
  (20.6 ms); hi-Z SSR 3.2 ms + 0.35 ms.
- Limits: non-glossy, planar only, normal maps approximated, needs
  authored shapes. Bonus slides: many parallel shapes, glass panels with
  an atlas, normal maps by a short march.

# For this fork

Implemented from the talk (no code taken) in
`shaders/effects/planar_reflections.glsl`: atomic max of the inverted key
(so a cleared buffer is 0), offsets to the exact projected position (no
fraction mirroring), points within the reflector's thickness skipped, the
reflector at each pixel matched by depth so transparent water works, the
last frame's color reprojected, roughness blur through its mipmaps. See
REFLECTIONS.md.

[^talk]: Cichocki 2017
