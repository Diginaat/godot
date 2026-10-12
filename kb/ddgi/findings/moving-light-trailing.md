---
type: Finding
title: Indirect light trails behind a moving lamp
description: In a large volume the GI of a moving emissive object lags and breaks into probe-sized blotches; two fixes tried and rejected, the cause and the open options.
tags: [ddgi, moving-lights, visuals, open-issue]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 22
---

# What it looks like

A bright emissive sphere circling in a storm-darkened game level (about
36,000 probes, 1 m spacing, baked + realtime updates). In the GI buffer,
the glow on the floor sits where the sphere was a moment ago, and breaks
into blotches the size of the probe spacing. Some runs show the GI
staying at about 60% of its usual level for 3 seconds while the sphere
moves.

# What limits the response (interior test, `--emissive_step=1`)

| Stage | Effect |
| --- | --- |
| Probe updates | 50% after 4 frames, 90% after 47-50 frames without display smoothing |
| Display smoothing (`light_transition_time` 0.2 s = 12 frames) | Adds about 20 frames: 50% after 19-34, 90% after 63-83 |
| Hysteresis tail | Below a 10% gap a probe closes only 5% per update (hysteresis 0.95): about 30 updates for the last 10% |
| Budget in big volumes | Each probe is updated every 15-20 frames; a probe only reacts fast after its own update saw the change twice |

# Tried and rejected

| Idea | Result |
| --- | --- |
| Display smoothing that follows large gaps fast | 50% after 17-25 frames (before 26-29), 90% unchanged. The weather fade showed probes stepping one by one: the worst frame had 0.1% of pixels jump 8-10 levels (before 1) |
| Flag the 6 neighbors of a probe with a large confirmed change for the next frame | Storm level: settled GI varied 17.4-27.6 between runs (before 21.4-22.8), stale dark blocks and sky-lit walls in some runs. Cause not investigated |

# Options not tried yet

- Lower the 10% offset in the hysteresis drop only for probes near a
  moving emissive object (known positions from the scene), so noise in
  other probes doesn't darken rooms (why the offset exists).
- Let the scheduler trace probes near moving emissive instances first:
  the engine knows which instances moved, so no feedback loop is needed.
- Screen-space or per-pixel near-field light for emissive surfaces
  ([screen-space probes](/ddgi/improvements/screen-space-probes.md)).
