---
type: Proposal
title: Interior volumes against thin-wall leaks
description: Artist-placed interior volumes tag probes and surfaces as inside or outside; sampling ignores probes of the other kind.
tags: [ddgi, light-leaks, authoring, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
  - id: enlisted
    resource: /research/enlisted-scalable-gi.md
    title: Enlisted scalable GI
  - id: enemies
    resource: /research/enemies-adaptive-probe-volumes.md
    title: Enemies adaptive probe volumes
---

# Today

DDGI.md lists the limit: light leaks when a wall is thinner than about a
quarter of the probe spacing, or open (single-sided). Distance moments and
relocation help but can't separate two sides closer than the probe grid
resolves.

# Proposal

Three shipped games use the same idea: tell the system which side is
inside.[^yotei][^enlisted][^enemies]

1. A `DDGIInteriorVolume` node (box or convex mesh). Probes whose position
   is inside any volume are interior, others exterior (computed on the
   CPU when volumes change, uploaded as a bit per probe).
2. Pixels: interior if inside a volume (test in the apply pass against a
   small volume list, or stencil-render the volumes as Ghost of Yotei
   does). A blend band at doors and windows (fade over a few cm at the
   volume surface).
3. Sampling uses only probes of the pixel's kind; if none of the 8 match,
   fall back to the matching probes of the next coarser cascade, then to
   all probes.
4. Probe rays: no change (rays see real geometry).

Cheaper first step from Enemies: "touch-up" volumes that just mark probes
inside them invalid (virtual wall thickening), using the existing inside
state.

# Measure

A test view with a 5 cm wall between a bright outdoor area and a dark
room, with and without volumes; apply pass cost.

[^yotei]: Ghost of Yotei
[^enlisted]: Enlisted
[^enemies]: Enemies
