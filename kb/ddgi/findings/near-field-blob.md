---
type: Finding
title: Probes put nearby bounce light in the wrong place
description: Bounce light from an object close to a surface comes out as a lagging blob around the nearest probes; the probes' red glow in the first bleed setup was even light leaking from the ball's sunlit top.
tags: [ddgi, visuals, moving-objects]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 25
---

# What it looks like

Bleed view of the test project: a red ball bouncing in front of a white
wall. In the GI buffer the red on the wall is a round blob about two
probe spacings wide, above and behind the ball, and it trails the ball
by many frames.

# Why

- A surface point blends the 8 probes around it. A probe sees the ball
  from its own position, not from the wall: the red goes where the
  probes are, spread by the trilinear weights.
- Probes blend new rays with hysteresis, and surfaces see a display copy
  that eases toward them (`light_transition_time`). Realtime updates (all
  probes every frame) and 0.5 m spacing changed almost nothing: the lag is
  the blending, not the schedule.
- Hysteresis 0.5 and no easing made the red follow faster, but every
  surface went blotchy (probe noise is no longer averaged out).

# The red glow can be wrong, not only late

In the first bleed setup the sun came from behind the wall: the ball's
side toward the wall lay in the wall's shadow and only its top was lit.
The probes still showed a red glow on the wall (a probe above the ball
sees the lit top), but rays from the wall to the ball found its shadowed
side, and the path tracer showed almost no red. Check the light path
before calling missing bounce light a bug.

# Fix

[Near-field rays](/ddgi/improvements/near-field-rays.md): short rays per
block of the apply pass, accumulated and filtered, in place of the
probes' light near surfaces.

A first version took, for rays that hit nothing within range, the probes'
irradiance at the ray's end facing along the ray. Its cosine lobe also
takes in what lies to the side, so rays grazing a sunlit floor brought
the floor's light back and the floor came out too bright. The probes'
irradiance at the block itself has no such bias.
