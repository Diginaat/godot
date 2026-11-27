---
type: Finding
title: Where a skinned crowd's renderer cost goes
description: 1000 skinned characters (16 bones, 4608 vertices) posed every frame. GPU skinning already runs only for instances some view culled in (zero dispatches with none in view) and costs 0.15 ms for the whole crowd. Render setup CPU stays at about 0.5 ms with none in view: 0.27 ms of scene instance updates (skinned AABBs, BVH moves) and 0.16 ms of skeleton uploads. Bone AABBs are now kept in skeleton space.
tags: [skinning, animation, cpu, renderer]
status: stable
sources:
  - id: bench
    title: devtools/test/skin_bench.ps1 (misc/skin_bench_project), RTX 5070, 2026-11-27
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Measured

`skin_bench`: 1000 tubes, 16 bones each, 4608 vertices, a sun, 1920x1080,
240 frames per view, medians, ms. The skinning stage runs from the
"Skinning" timestamp to "Skinning End" (direct RD timestamps in
`MeshStorage::update_mesh_instances()`). Setup is
`RenderingServer.get_frame_setup_time_cpu()`.

| Driver | View | GPU frame | Render CPU | Setup CPU | Skinning |
| --- | --- | --- | --- | --- | --- |
| Vulkan | all | 1.519 | 1.31 | 0.60 | 0.149 |
| Vulkan | a tenth | 0.935 | 0.80 | 0.56 | 0.083 |
| Vulkan | none | 0.106 | 0.10 | 0.49 | none run |
| D3D12 | all | 1.655 | 1.32 | 0.58 | 0.203 |
| D3D12 | a tenth | 1.054 | 0.81 | 0.56 | 0.111 |
| D3D12 | none | 0.233 | 0.11 | 0.56 | none run |

- **GPU skinning is visibility-driven already.** The scene cull calls
  `mesh_instance_check_for_update()` only for instances that a camera or a
  shadow pass culled in. The "a tenth" view still skins about half the
  crowd, because the sun's cascades see it. All dispatches share one
  compute list with no barriers.
- **Setup CPU does not depend on what is visible.** A one-off
  instrumented run with none in view: `_update_dirty_instance()` took
  0.27 ms (the skinned AABB from bone AABBs, the world AABB, the BVH move)
  and `update_dirty_resources()` 0.16 ms (1000 skeleton `buffer_update()`
  calls plus their dependency notifications), so about 0.3-0.4 us per
  character.

# Change

`mesh_get_aabb()` transformed every bone AABB into skeleton space on
every call, then by the bone. That first transform is fixed, so it now
happens once in `mesh_add_surface()` (`skeleton_bone_aabbs`, with
`skeleton_to_mesh_xform` instead of a per-call `affine_inverse()`). The
results are identical: the same operations on the same inputs. In the
instrumented run, instance updates went from 0.498 to 0.446 ms. The setup
time in the table is within its run-to-run spread.

# Next

- One skeleton buffer for all skeletons (one upload, an offset in the
  skinning push constant) would remove most of the 0.16 ms.
- Animation CPU (AnimationMixer, Skeleton3D pose updates) on the main
  thread is not measured here (the bench poses bones from GDScript).
