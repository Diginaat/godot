---
type: Proposal
title: Adaptive probe density
description: More probes near geometry and fewer in open space, through bricks or an octree, instead of uniform grids.
tags: [ddgi, placement, proposal]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: enemies
    resource: /research/enemies-adaptive-probe-volumes.md
    title: Enemies adaptive probe volumes
  - id: yotei
    resource: /research/ghost-of-yotei-ambient.md
    title: Ghost of Yotei ambient lighting
  - id: forza
    resource: /research/forza-adaptive-probe-placement.md
    title: Forza adaptive probe placement
---

# Idea

All three productions place probes densely near geometry and sparsely in
open air: SDF-guided 4x4x4 bricks (Enemies), octrees with at most one level
between neighbours and parent blending at faces (Ghost of Yotei),
procedural point clouds (Forza).[^enemies][^yotei][^forza]

# Fit for this fork

- Our classification already makes empty-space probes cheap (inactive,
  fixed rays only) but they still cost atlas memory and sampling stays on
  the uniform grid.
- For fixed `DDGIVolume`s (and baking) a brick or octree layout is
  possible; for scrolling cascades it is much harder.
- Large change to addressing, sampling, scheduling and baking. Only if
  memory or detail near geometry becomes the main limit.

[^enemies]: Enemies
[^yotei]: Ghost of Yotei
[^forza]: Forza
