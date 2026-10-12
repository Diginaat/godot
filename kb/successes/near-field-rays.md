---
type: Success
title: Near-field rays fixed the lagging bounce-light blob
description: Short per-block rays traced with the existing probe-ray shading, plus a small temporal and spatial filter, put nearby bounce light where it belongs and let it follow a moving object.
tags: [success, ddgi, visuals]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What was done

Instead of tuning the probes (realtime updates, finer spacing, lower
hysteresis: no help, or blotchy), the apply pass got its own short rays,
one per low resolution block and frame, traced by the same ray tracing
pipeline and hit shading as the probe rays. Rays that hit nothing take
the probes' light, so only surfaces near geometry change. Details:
[near-field rays](/ddgi/improvements/near-field-rays.md).

# Why it worked

- Reuse: the raygen only needed a new mode; the closest hit, lights,
  materials and bounce data were already there. The update list and ray
  bindings carried the rays, so the ray tracing uniform set didn't change.
- The probes stay the far field and the noise-free answer for open
  surfaces; the rays only add what the probes can't place.
- A dedicated test scene (red ball, white wall, GI view, animated contact
  sheet, probes on/off, path tracer) showed the problem and the fix at a
  glance.

# Reuse

For any "light in the wrong place near objects" problem, look at the
light path with the path tracer first, then add rays at the surface
rather than more probes.
