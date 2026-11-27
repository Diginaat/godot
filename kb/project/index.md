# Project

* [Forward+ shader variants and pipelines compiled only where used](/project/shader-permutations.md) - feature groups, per-shader skipped variants, pruned transparent precompiles, full-key pipeline maps: variants per material 28 to ~4, cold stall 146 to 95 s, warm 2.4 to 0.65 s, memory 353 to 84 MB

* [Scene runtime optimization](/project/scene-runtime.md) - groups, children cache, transform propagation epoch, group storage, process prefetch: instancing and churn 10-11x faster at 100k nodes, API unchanged

* [Unique array baseline and bulk-copy candidate](/project/unique-array-evaluation.md) - Windows correctness passes; 12-size boundary sweep finds small-copy regressions and blocks performance acceptance

* [Core modernization phase-zero checkpoint](/project/core-modernization-audit.md) - baseline/branch, compatibility gates, benchmark tools, native-map direction and resumable phase plan

* [Packed core containers and compatible hashing](/project/core-template-storage.md) - dropped 2026-11-15: packed storage and robin-hood indices made numeric hash tables slower and bigger; only the String-key byte hash was kept
* [Core template migration benchmarks](/project/core-template-benchmarks.md) - 24-case controlled A/B: the dropped migration, and the kept String-key byte hash (integer tables unchanged, String hits 0.55-0.62x)
* [Where engine CPU time goes in core code](/project/core-cpu-profiles.md) - sampling profiles of five workloads: hash lookups 3-16%, scene instancing 21% in RID checks, locks and refcounts plus 14% heap; the callers' patterns cost more than container internals

* [Project overview](/project/overview.md) - what this fork is, its sources, branches and release flow
* [Modern compact editor theme](/project/modern-compact-editor-theme.md) - neutral charcoal native theme with compact spacing and restrained interaction accents
* [Native indirect foundation](/project/native-indirect-foundation.md) - existing GPU submission seam, sentinel defect and per-view culling constraints
* [Forward+ material parameter tables](/project/material-parameter-tables.md) - one storage-buffer slot per material, shared uniform sets; unique_10k 8,776 draws to 11, render CPU 7.5-8.1 to 0.42-0.47 ms
* [Native GPU geometry statistics](/project/gpu-geometry-statistics.md) - async per-viewport culling/CLOD/HZB counters, only while polled
* [Native MultiMesh culling](/project/native-multimesh-culling.md) - opt-in GPU frustum tests, stable IDs, viewport-owned commands and fallback
* [Cluster hierarchy cooker](/project/cluster-hierarchy-cooker.md) - source-index LOD trees, boundary locks, checked resource and complete reference cuts
* [GPU cluster selection](/project/gpu-cluster-selection.md) - ancestor-veto cuts, projected-error/frustum selection and guarded native commands
* [Two-pass MultiMesh occlusion](/project/multimesh-two-pass.md) - hidden-last-frame instances left out of the depth pre-pass; exact; street grid 7-12% faster, open valley 1-7% slower
* [Cluster LOD skips repeat-batched elements](/project/clod-skipped-for-batched-elements.md) - shared meshes with same-shader materials batched since material tables, so no CLOD; fixed (G5): CLOD-eligible surfaces are not batched
* [CLOD two-pass costs more than it saves](/project/clod-two-pass-zero-draws.md) - one indirect record per group in every pass, mostly empty; two_pass 27-32% slower than off in hzb_bench; resolved by compact command streams
* [Attachment VRS on D3D12 did nothing, then used the wrong tile size](/project/d3d12-vrs-attachment.md) - rate image never bound and 8 vs 16 pixel tiles (upstream bugs too); fixed, adaptive VRS 47.5-47.9 dB on both drivers
* [Where a skinned crowd's renderer cost goes](/project/skinned-crowd-costs.md) - GPU skinning is visibility-driven already (0.15 ms for 1000 characters); setup CPU about 0.5 ms regardless of view; bone AABBs kept in skeleton space
* [Keep the TLAS in frames where nothing changed](/project/tlas-static-skip.md) - DDGI and path tracer skip the TLAS build when instances and BLASes are unchanged; static interior AS stage 0.094 to 0.007 ms
* [Native property access paid for MethodBind::call() checks](/project/native-property-validated-call.md) - getters and exactly typed setters through validated_call(); scene_bench script 100k 23.6 to 22.1 ms
* [Moving instances paid six CRT fmodf calls each for their BVH bounds](/project/moving-instance-quantize.md) - power-of-two quantize with floor, same values; city_extreme frame 8.55 to 8.42 ms
* [Every instance table record carried a previous transform](/project/prev-transform-table.md) - previous transforms only for instances that moved, in their own compacted table, and no lightmap UV scale; record 160 to 96 bytes, city_extreme 232 to 219 MB
* [The GPU path kept a 176-byte instance copy per member](/project/gpu-instance-data-in-shader.md) - the scene shader builds it from the table record, member and bucket; city_extreme 287 to 232 MB and -4% GPU time
* [VRAM stayed at the largest scene's size after it was unloaded](/project/vram-shrink-after-unload.md) - slots, GPU instance buffers, CPU list instance buffers, the upload staging ring and transfer worker staging shrink; small_1k after a 100k scene 300 to 83 MB, city_extreme 415 to 287 MB
* [The HZB build was bound by barriers between its dispatches](/project/hzb-fused-reduce.md) - four levels per dispatch in shared memory; small_1k HZB 0.018 to 0.010 ms, forest_map 0.033 to 0.021 ms, levels bit-identical
* [Fork code in the ubershader made the cold shader stall longer](/project/ubershader-cold-compile.md) - instance data loaded once per stage, DDGI capture as a pipeline constant of its own ubershaders; ubershader driver CPU -9%, cold stall 48.4 to 44.3 s
* [Reading a global transform recursed once per dirty ancestor](/project/global-transform-loop.md) - dirty ancestors updated in a loop, top down; scene_bench deep 100k 18.7 to 15.1 ms
* [Scripted node processing: allocation and unprefetched lines](/project/process-loop-misses.md) - GDScript notifications without an allocation, process loop prefetch of the script instance and Node3D transform; script 100k 36.1 to 23.6 ms
* [VRAM of the GPU-driven systems cut](/project/gpu-buffer-vram.md) - buffer growth with an eighth of headroom, HZB at half resolution; city_extreme 475 to 415 MB, a 1k-node scene 80 to 72 MB (stock 69)
* [Render-thread cost of many lights halved](/project/light-fill-cpu.md) - cached linear light color, radix depth sort, instanced cluster draws (and the cluster shader fix they needed); 4096 lights 0.89 to 0.47 ms CPU, same image
* [The cluster render buffer sized by the limit cost 0.27 ms](/project/cluster-render-buffer-used-size.md) - the intermediate cluster buffer was cleared at the full limit every frame (172 MB at 1080p, limit 4096); now laid out for the used elements
* [Lights over the cluster limit keep the nearest](/project/light-cap-nearest.md) - visible positional lights above `max_clustered_elements` were dropped in cull order; now the farthest by range are left out, with a warning
* [Receiver culling of directional shadow casters](/project/shadow-receiver-culling.md) - light-space receiver masks from the camera depth skip GPU-path casters that shade nothing visible; adaptive; same image; city GPU frame 17-22% faster with every cascade per frame, neutral otherwise
* [Cluster LOD in shadow passes](/project/shadow-clod.md) - every cascade, spot and cube face selects its own cut with the error in shadow-map texels; shadow stage 9-11x faster, GPU frame 2.2-2.6x in shadow_clod_bench
* [Compact CLOD command streams](/project/compact-clod-commands.md) - selection and HZB kernels append only drawn groups, passes draw with a GPU count; hzb_bench GPU frame 2.2-5.5x faster on Vulkan, 1.6-3.3x on D3D12
* [Adaptive variable rate shading](/project/adaptive-vrs.md) - GPU picks each tile's shading rate from the previous frame; main-viewport default; 2-5% on the valley, 3.6x on an expensive-shader floor, ~47 dB PSNR
* [GPU occlusion for CPU render list meshes](/project/mesh-occlusion.md) - HZB-tested indirect draws for every opaque CPU-list mesh (lightmapped, skinned, fading...); exact images, 20 of 20 hidden meshes skipped; batches kept local, street bench GPU frame 1.043 to 0.570 ms
* [Resident CLOD raster](/project/resident-clod-raster.md) - native storage, viewport commands, original vertex/material pipelines and conventional fallback
* [Cluster scene import](/project/cluster-scene-import.md) - final-source cook/cache, shared meshes, unsupported fallback and optimized shadows
* [Current-frame CLOD HZB](/project/current-frame-clod-hzb.md) - farthest-depth reduction, opaque-only rejection and conservative current-frame recovery
* [Residency cut protocol](/project/residency-cut-protocol.md) - complete coarse fallback, evicted ancestors and GPU request feedback
* [CLOD pages from disk](/project/clod-disk-paging.md) - checked page file, root-only memory, worker reads and dropped stale reads
* [GPU voxel levels for the far field](/project/far-field-gpu-voxel-levels.md) - per-brick level selection in compute, indirect MultiMesh discs, exact CPU mirror
* [Far directional cascades kept between frames](/project/directional-cascade-cache.md) - far cascades every 2nd/4th frame, staggered, reused while the slice fits the cached region; -1.1 to -1.5 ms in the valley
* [Coarser mesh LODs for shadow casters](/project/shadow-lod-scale.md) - shadow passes use a 4x LOD threshold; -0.4 ms in the valley; grass draw order and frame timestamps tried without effect
* [World wind and weather core](/project/world-wind.md) - WindServer gusts shared by foliage, cloth and bodies; Weather node, profiles, effects, borrowed scene values; weather sky and drifting clouds; rain, occlusion map, wet surfaces; ground haze and mist motes; lightning; synthesized sound; local wind zones
* [Built-in level streamer](/project/level-streaming.md) - LevelStreamer3D/LevelStreamCell3D: Inspector cells, export-discovered scenes, threaded load and instantiation, bounded activation, readiness, persistent door state; exported acceptance 6/6 pass, streamed p99 equal to preloaded
* [Stock 4.7.2 vs the fork, 2026-11-27](/project/stock-compare-2026-11.md) - game levels 2.9-6.0x, instances up to 15.9x, scene runtime up to 19x, PhysX GPU 2.7x over Jolt at 30k; where the fork is worse
