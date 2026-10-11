---
type: Finding
title: Render graph ordering can drop the DDGI bounce light
description: Compute work recorded before the DDGI apply pass made the probes lose their bounce light; cause not found, avoided by ordering.
tags: [ddgi, render-graph, synchronization, open-issue, xegtao]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-log
    resource: ../../../DDGI.md
    title: DDGI.md findings log, 2026-10-11
---

# What happened

While adding XeGTAO (branch `dev-ddgi-ao`), XeGTAO's main compute pass ran
before the DDGI apply pass in the same frame. The DDGI probes then lost
their bounce light: interior test view, GI buffer mean 13.4 down to 8.1
after 600 frames, the same as with bounce energy 0. Budgeted and realtime
updates both.[^ddgi-log]

# What it isn't (tested, converged at 600 frames)

| Test | Result |
| --- | --- |
| XeGTAO quality, resolution, radius, intensity 0, dummy normal buffer | Same 8.1: not the AO values |
| Only the XeGTAO depth prefilter | Fine: the main pass triggers it |
| Godot SSAO instead | Fine |
| Shader execution reordering off | Still dark |
| `FORCE_FULL_ACCESS_BITS` in the render graph | Still dark |
| Explicit RT buffer dependencies (`ray_bounce`, `probe_buffer`, `update_list`) | Still dark |
| Khronos synchronization validation | No hazard reported in the DDGI passes |

# What fixes it

Any of: `GODOT_RG_REORDER=0`, `GODOT_RG_FULL_BARRIERS=1`, a graph
synchronization point at any DDGI step, or running XeGTAO after the DDGI
apply pass (shipped). So some synchronization the render graph derives
around the DDGI passes is too weak once other compute work shares their
levels. Cause not found.

# Consequences

- XeGTAO runs after the GI processing; bent normals can't feed the DDGI
  lookup in the same frame.
- Any compute work added before DDGI (compositor effects, new features) may
  trigger it again. Check DDGI brightness converged, with and without the
  new work, after adding a pass near it.

# Next steps

- A GPU capture (for example Nsight Graphics) of the dark case.
- Read how the graph places the RT list and the bounce compute pass in
  levels in the dark case (`PRINT_RENDER_GRAPH` with labels).

[^ddgi-log]: DDGI.md findings log
