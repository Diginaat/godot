---
type: Overview
title: DDGI improvement proposals, ranked
description: Improvements for this fork's DDGI suggested by the GI papers and talks read on 2026-10-12, ranked by value for cost.
tags: [ddgi, proposals, roadmap]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# Ranking

Value is for this fork's DDGI (dynamic, hardware ray traced, uniform
grids, scrolling cascades, baking). Cost is a rough guess before any code.
Status per proposal in the last column.

| # | Proposal | Fixes | Cost | From | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | [Sample-driven probe updates](/ddgi/improvements/sample-driven-updates.md) | Probes that pixels use update first; faster visible convergence, less wasted budget | Small | Ghost of Yotei | Done (`dev-ddgi-sched`) |
| 2 | [Initialize scrolled probes from the coarser cascade](/ddgi/improvements/scroll-init-from-coarser.md) | Finer cascade usable at once after scrolling; fewer blotches at the scroll edge | Small | Enlisted | Proposed |
| 3 | [Interior volumes against thin-wall leaks](/ddgi/improvements/interior-volumes.md) | Leaks through walls thinner than a quarter of the spacing (a known limit) | Medium | Ghost of Yotei, Enlisted, Enemies | Proposed |
| 4 | [Contact GI on XeGTAO](/ddgi/improvements/contact-gi.md) | AO only darkens; bounce light at contacts is lost | Medium | SDFDDGI | Proposed |
| 5 | [Neighbour blend for inside probes](/ddgi/improvements/inside-probe-neighbour-blend.md) | Black or strip artifacts where several probes are inside geometry | Small | Ghost of Yotei, Enemies | Proposed |
| 6 | [Screen-space probes for near-field detail](/ddgi/improvements/screen-space-probes.md) | Detail below the probe spacing; contact bounce | Large | Ghost of Yotei | Proposed |
| 7 | [Adaptive probe density](/ddgi/improvements/adaptive-density.md) | Uniform density wastes probes in open space, lacks them near geometry | Large | Enemies, Ghost of Yotei, Forza | Proposed |
| 8 | [Compressed baked data](/ddgi/improvements/compressed-baked-data.md) | Baked data size | Small to large | Gaussian compression, Enemies | Proposed |

Small ideas not worth their own page yet:

- **Seam dithering** (Enemies): jitter the sample position in the tangent
  plane with animated interleaved gradient noise near cascade borders;
  TAA hides it. Our cascades already blend over their edges, so only if
  seams show.
- **First-seen probes at lower ray count** (Enlisted): a separate cheap
  queue for never-traced probes.
- **History rejection by normalized luminance difference, averaged per
  probe** (Ghost of Yotei, from AMD GI-1.0): compare with our light change
  detection before changing anything; DDGI.md warns that value-adaptive
  blending biases unless the change stands out of the noise.

# Order of work

Start with 1 and 2 (small, measurable with the existing test project:
convergence frames, flicker). Then 3, which needs a new node and
authoring. 4 belongs with the XeGTAO branch.
