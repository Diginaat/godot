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

## OD8 (2026-11-27): native properties use validated calls when the types fit

`Object::get_native()` and `set_native()` called getters and setters with
`MethodBind::call()`, which checks and converts every argument. Getters
now use `validated_call()` (their arguments are known), and setters do
when the value has the declared type. Objects, arrays, dictionaries,
converted values, vararg binds, default arguments and returning setters
keep `call()`. Done in core, not as compile-time calls in GDScript: a
script on a subclass can meet a property registered again, which the
per-object member lookup handles. scene_bench script 100k -6%.

## OD9 (2026-11-27): buffers and the staging ring shrink after a large scene is gone

GPU instance slots, the GPU instance buffers, the CPU render list instance
buffers, the RenderingDevice upload staging ring and the transfer worker
staging buffers only grew. A small scene after a 100,000-node one kept
228 MB of VRAM it did not use. Now:

- Slots are taken lowest first, and the slot range ends at the highest
  slot in use.
- `gpu_buffer_fits()`: content-sized buffers over 16,384 elements are
  recreated when under a quarter is needed, with the usual eighth of
  headroom.
- Render list instance buffers shrink after 120 frames under a quarter.
- `_trim_staging_blocks()` frees upload staging blocks past twice the
  busiest frame of the last 300, never one a frame in flight can read.
- `_trim_transfer_workers()` frees a transfer worker's staging buffer
  (sized for the largest transfer it ever made) after 300 frames without
  a transfer.

The thresholds leave a factor of four (two for staging) between growing
and shrinking, so steady content never resizes. Both trims change
upstream code in `RenderingDevice`. small_1k after massive_100k 300 to
83 MB; level bench city_extreme 415 to 287 MB, forest 221 to 165 MB;
GPU time unchanged.

## OD10 (2026-11-27): the scene shader builds the GPU path's instance data

The GPU instance cull wrote a 176-byte `InstanceData` per member every
frame, 55 MB at 200k nodes, all copied from the instance table record,
the member and its bucket. The scene shader now reads those (bindings 47,
49 and 50) and a 16-byte view flags buffer (51), and builds the data once
per stage (`gpu_instance_data()`, `scene_instance_load()`). Specialized
CPU-path pipelines are unchanged; the ubershader branches as before.
city_extreme 287 to 232 MB, massive_100k GPU -5%, cold shader stall
unchanged, images and motion vectors equal to the CPU path (also with
the ubershader forced). Cost: three more storage buffer bindings in the
render pass set.

## OD11 (2026-11-27): global transforms of dirty chains update in a loop

`Node3D::get_global_transform()` recursed into the parent for each dirty
level (a call, a 48-byte return copy and a dirty check per level).
`_update_global_transform_chain()` collects the node and its dirty
ancestors (64 per pass; longer chains resolve their upper part first)
and updates them top down in place, with the same local, top-level and
`disable_scale` rules. scene_bench deep 100k 18.7 to 15.1 ms, deep 10k
-7%, other scenarios within spread. A unit test covers chains over 64
levels (it fails with the overflow step removed).

## OD12 (2026-11-27): previous transforms only for instances that moved

The instance table record carried a 48-byte previous transform for every
slot, though it equals the transform for any instance that did not move
last frame. `GPUInstancePrevTable` holds entries only for instances whose
previous transform differs; the record (160 to 112 bytes) points to its
entry from bit 8 of `gpu_flags`, and the scene shader (binding 52) falls
back to the transform. The table is compacted when fewer than half its
used entries are live: without that, entries of instances that stopped
moving left holes and 2,000 movers uploaded 20,000 entries a frame.
city_extreme 232 to 222 MB, churn_100k 131.5 to 120.3 MB, massive_100k
107 to 102 MB; GPU time unchanged. Cost: one more storage buffer binding
in the render pass set and one more 48-byte read for moved instances.

## OD13 (2026-11-27): no lightmap UV scale in the instance table record

The record's lightmap UV scale (16 bytes) is read by the scene shader
only for instances with `INSTANCE_FLAGS_USE_LIGHTMAP`, and the GPU path,
the only reader of records, takes no lightmapped instances. Dropped:
112 to 96 bytes per slot (three 32-byte sectors). city_extreme 222 to
219 MB, churn_100k 120.3 to 116.6 MB; GPU time within spread. If the GPU
path ever takes lightmapped instances, the scale goes back (or into a
side table like OD12's).

## OD14 (2026-11-27): moving instance bounds quantized without fmodf

`RendererSceneCull::_update_instance()` quantizes a moving instance's
BVH bounds with a power-of-two unit; `AABB::quantize()` did it with six
CRT `fmodf` calls (15% of `_update_instance()` in city_extreme).
`_aabb_quantize_pow2()` uses `x - floor(x / unit) * unit`, the same
value for a power-of-two unit (checked over 259 million float32 values),
with `AABB::quantize()` kept for units below `CMP_EPSILON`. city_extreme
frame 8.55 to 8.42 ms, render CPU 5.23 to 5.10 ms; town unchanged.
