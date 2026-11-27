---
type: Finding
title: Render graph ordering can drop the DDGI bounce light
description: Render graph reordering around the DDGI passes dropped the bounce light (2026-10, avoided by ordering) and made the probes flicker (0.10.1, fixed by a timestamp fence after DDGI); the untracked access is not found.
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

# Seen again: flicker in spots (2026-11-27)

Since 0.10.1 (GPU geometry compute passes before the colour passes, on by
default) the probes flicker: the interior flicker test (600 + 60 frames)
gives `flicker_std` 0.55-1.0 and `flicker_px` 8-16%, against 0.12-0.13
and 0.18% on the 0.8.1 and 0.9.1 release editors. The brightened image
shows a round blotch per probe on floor, walls and ceiling; the
converged mean is unchanged (0.0117-0.0122). `--gi=none` doesn't flicker.
Turning off one GPU geometry path at a time (`gpu_instances`,
`gpu_instance_table`, `mesh_occlusion`, `multimesh_gpu_culling`,
`cluster_lod`) or VRS changed nothing. On the same build, 2 alternating
runs each:

| Variant | flicker_std | flicker_px |
| --- | --- | --- |
| default | 0.595-0.720 | 9.60-13.02% |
| `GODOT_RG_REORDER=0` | 0.130-0.135 | 0.19% |
| `GODOT_RG_FULL_BARRIERS=1` | 0.129-0.130 | 0.19% |

Same class of cause; this time it shows as noise, not a dark result. The
smoke test printed the bad `flicker_px` for weeks without failing: it
had no threshold.

Narrowed with a temporary hook that put a graph synchronization point at
any label or DDGI step (`add_synchronization()`, chosen by an environment
variable, one build):

- a sync point anywhere from the TLAS build to the start of the opaque
  pass fixes it; at render setup, the depth pre-pass, the sky or TAA it
  doesn't;
- a one-frame dump of every command's level and tracked resources showed
  no misordered pair among tracked resources (only shadow atlas region
  draws share a level, as designed). So the hazard is an access the graph
  doesn't see. The ray tracing shaders read vertex, index and material
  data through buffer device addresses, which the graph can't track;
- a timestamp at the start of DDGI doesn't fix it (a timestamp only
  orders the commands since the previous timestamp, so the TLAS build and
  the DDGI buffer updates stay free); a timestamp after the DDGI apply
  pass does.

Fixed with a fence: `capture_timestamp("After DDGI")` at the end of
`RenderForwardClusteredPT::_ddgi_process()`, so work recorded after DDGI
can't be reordered into it. Interior flicker test, 6 runs: `flicker_std`
0.125-0.140, `flicker_px` 0.19-0.26%. Cost (room view, 1080p,
`--gpu-profile`, alternating with the build without the fence): GPU frame
1.313-1.323 against 1.313-1.331 ms, `ddgi_ms` 0.532-0.535 against
0.531-0.537 ms. Brightened captures of two consecutive frames: the probe
blobs that jumped between frames are gone. The smoke test now fails above
1% `flicker_px` (fixed build 0.00-0.32%, without the fence 12.94%).

Still open: which buffer is read without a dependency. A fence after DDGI
does not protect DDGI from work recorded before it; the October case
(XeGTAO before the apply pass) was avoided by moving XeGTAO.

# Next steps

- A GPU capture (for example Nsight Graphics) of the dark case.
- Read how the graph places the RT list and the bounce compute pass in
  levels in the dark case (`PRINT_RENDER_GRAPH` with labels).

[^ddgi-log]: DDGI.md findings log
