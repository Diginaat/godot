---
type: Proposal
title: Initialize scrolled probes from the coarser cascade
description: When a cascade scrolls, fill the new probes' atlas texels from the next coarser cascade so they can be sampled before their first trace.
tags: [ddgi, scrolling, cascades, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: enlisted
    resource: /research/enlisted-scalable-gi.md
    title: Enlisted scalable GI
---

# Today

Scrolled-in probes get state `DDGI_PROBE_SCROLLED`: not sampled until
traced (their atlas texels still hold the probes that scrolled out at the
far side), the coarser cascade covers them meanwhile, and their first
update replaces the texels. Moving fast, the fine cascade has holes along
the scroll edge and the image switches from coarse to fine light as probes
get traced.

# Proposal

Enlisted copies the coarser cascade into newly covered finer texels and
marks them "not really computed" so they are relit when visible.[^enlisted]

1. In the schedule pass (which already resets scrolled probes), or a small
   pass after it, sample the next coarser cascade's irradiance (and
   distance moments) in each texel's direction at the probe's position and
   write it into the new probe's texels.
2. Make these probes sampleable at once (a new state, for example
   `DDGI_PROBE_SEEDED`), keep their high scheduling priority, and keep the
   "first update replaces" rule so the seed doesn't linger.
3. The coarsest cascade has nothing coarser: keep today's behavior there.

# Measure

Outdoor `--move=1`: the error left by moving (`--settle` shot difference),
blotches at the scroll edge, flicker.

[^enlisted]: Enlisted scalable GI
