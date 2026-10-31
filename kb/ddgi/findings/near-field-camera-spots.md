---
type: Finding
title: Near-field rays left dark spots that moved with the camera
description: The near-field rays start from visible surfaces and keep a screen-space history, so moving the view showed dark spots that traveled with it; turning them off removed the spots, and they were removed from the engine.
tags: [ddgi, visuals, near-field, camera]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
sources:
  - id: nf
    resource: /ddgi/improvements/near-field-rays.md
    title: Near-field rays for the DDGI apply pass
---

# What was seen

In a game level with a fixed `DDGIVolume` (no `follow_camera`), moving the
editor camera showed dark spots and light bleeding that scrolled with the
view. Older builds (before 0.7.0) didn't show them.

# Cause

Turning off `rendering/global_illumination/ddgi/near_field_rays` alone
removed the spots (checked by eye in the editor, 2026-10-31). The probes
don't depend on the camera in that setup; the near-field rays do: one ray
per low resolution block from the visible surface, accumulated over frames
with reprojection. Where the view uncovers surfaces, the few fresh rays
are noisy and dark until the history fills in.

# Decision

Near-field rays are disabled in the engine, with no setting to turn them
on (`RenderDDGI::_near_field_rays()` returns false). The settings
`near_field_rays`, `near_field_distance` and `skip_moving_objects` were
removed. Moving objects stay in the probes again (`DDGI_SKIP_MOVING_OBJECTS`
false): nothing else lights them for DDGI, and a trailing indirect shadow
beats none. The shader and apply code of the rays is still there, unused.
The [blob they fixed](/ddgi/findings/near-field-blob.md) is back.
