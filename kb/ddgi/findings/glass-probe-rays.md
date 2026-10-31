---
type: Finding
title: DDGI rays through glass ended wrong
description: Four bugs on DDGI rays that meet refractive surfaces - miss distance on early stops, the path tracer's bounce limit, the glass tint dropped at the hit, and single-segment near-field rays.
tags: [ddgi, raytracing, glass, bugfix]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
---

# Bugs (fixed 2026-10-31)

1. `refract_and_bounce()` set `hit_t` only at its end. A ray stopped early
   (bounce limit, rough microfacet to the wrong side, zero throughput)
   kept `DDGI_MISS_DISTANCE`: the distance atlas saw open space at the
   glass.
2. Probe rays start at one bounce and were checked against the path
   tracer's `RT_GET_MAX_BOUNCES()`; a low setting stopped them at the first
   pane. They now skip it; the ray generation loop limits them with
   `DDGI_MAX_GLASS_SEGMENTS` (6, was a hard-coded 4: a double glazed
   window has 4 interfaces).
3. `ddgi_probe_ray_shade()` replaced the radiance and throughput, dropping
   the glass tint and emission (the miss shader did apply the tint). The
   hit's light and the bounce reflectance are now multiplied by it.
4. Near-field rays traced one segment and read glass as a black occluder.
   (Moot since the near-field rays were disabled; see
   [the camera spots finding](/ddgi/findings/near-field-camera-spots.md).)

Not measured: built and smoke tested only (no glass test scene yet).
