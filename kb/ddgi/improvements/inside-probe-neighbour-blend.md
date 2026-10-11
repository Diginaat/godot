---
type: Proposal
title: Neighbour blend for inside probes
description: Instead of skipping probes inside geometry, give them a blend of their valid neighbours' light at sampling time.
tags: [ddgi, sampling, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
---

# Today

`ddgi_sample_volume()` skips `DDGI_PROBE_INSIDE` probes. When most of the
8 probes around a point are inside (narrow spaces, thick walls with a thin
gap), the remaining weights are small, and the point can go dark or show
strips; DDGI.md's step 18 already fought black surfaces.

# Proposal

Ghost of Yotei culls probes inside geometry but replaces each at runtime by
a blend of its valid neighbours, which interpolates more smoothly than
skipping.[^yotei]

- After the blend pass, a small pass writes, for each inside probe, the
  average of its valid face neighbours into its atlas tile (or a flag
  plus neighbour list sampled indirectly).
- Sampling then treats inside probes as valid with a reduced weight.

Check first: count how often a point has 5+ inside probes in the test
views (probe states debug view); if rare, this isn't worth it.

[^yotei]: Ghost of Yotei
