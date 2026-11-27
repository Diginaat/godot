# Engine performance: decisions

## OD1 (2026-11-27): GPU buffer growth with an eighth of headroom

`gpu_buffer_capacity()`
(`servers/rendering/renderer_rd/forward_clustered/gpu_buffer_capacity.h`)
sizes the growing buffers of the GPU instance table, the GPU instance path,
mesh occlusion, MultiMesh culling and cluster LOD:

- Up to 65,536 elements it rounds up to a power of two.
- Above that, it adds an eighth and rounds up to a multiple of 16,384
  elements.

Powers of two doubled buffers just past a boundary: 262,145 members got
524,288 slots of 176 bytes, 88 MB for 46 MB of data. With an eighth of
headroom, reallocations stay rare while a level loads: about ten steps from
65k to 200k, each at most once per frame. No shader depends on the
capacities.

## OD2 (2026-11-27): the HZB starts at half resolution

The depth pyramid's mip 0 is now the farthest depth of each 2 x 2 pixels
of the depth buffer (`MODE_FIRST` in `mesh_hiz_reduce.glsl`), not a copy
of it. `hzb_box_occluded()` keeps full-resolution pixel coordinates,
starts at level 1 and reads mip `level - 1`. A coarser texel holds a
farther or equal depth, so the test stays conservative.

- Memory: a quarter (10.7 to 2.7 MB at 720p, about 85 to 21 MB at 4K).
- The pyramid is cheaper to build.
- Speed: the level bench was slightly faster on the GPU in all three
  levels.
- Occlusion: the counts in `mesh_occlusion_bench` did not change.

## OD3 (2026-11-27): GDScript notifications allocate only for scripts with `_notification()`

`GDScriptInstance::notification()` built a list of the inheritance chain
(a heap allocation) for every notification, before it looked for
`_notification()`. It now looks first and returns when no script in the
chain defines it. The order of the calls is unchanged when one does.

## OD4 (2026-11-27): the process loop prefetches what dispatch and scripts read

`SceneTree::_process_group()` prefetches two more lines of a node eight
places ahead: the script instance pointer and the first line after
`Node`. Four places ahead, for a node that is still in the tree, it reads
the script instance pointer and prefetches the script instance, the
instance ID and a Node3D's local transform. Extra lines go only to
scripted nodes: prefetching more for every node slowed native processing
of 100k nodes by 20-25% (more misses in flight than the core keeps).
