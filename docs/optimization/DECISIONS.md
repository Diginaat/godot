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

## OD5 (2026-11-27): the ubershader loads the drawn instance once per stage

`SCENE_INSTANCE(field)` chose the CPU or the GPU path's instance buffer
at each of its 57 uses. The ubershader can't fold that choice, so every
use compiled two loads. In the ubershader each stage now copies the drawn
instance once (`scene_instance_load()`). Specialized pipelines are
unchanged. Ubershader driver CPU -3.4% on the cold shader bench.

## OD6 (2026-11-27): DDGI probe capture is a pipeline constant of its own ubershaders

The DDGI sampling for reflection probe captures was a draw-time branch in
every ubershader pipeline. It is now pipeline constant 1 in the
ubershader. Ubershader keys of color passes keep that bit, and
`GlobalPipelineData::use_ddgi_probe_capture` precompiles the capture
ubershaders for the reflection probe format once a view uses DDGI. A
further -6% ubershader driver CPU. Cost: one more ubershader pipeline per
surface, compiled only in projects that capture probes with DDGI.

## OD7 (2026-11-27): the HZB reduces four levels per dispatch

One dispatch and one barrier per pyramid level made the HZB build
barrier-bound (11 levels, 0.018 ms in a 1k-node scene). Each 8 x 8 group
now also reduces its tile in shared memory to the next three levels;
`LEVELS` is a shader variant (eight pipelines), so a dispatch binds only
the levels it writes, never one image twice. Results are bit-identical
(`hzb.gd`). small_1k HZB 0.010 ms, forest_map 0.033 to 0.021 ms.
