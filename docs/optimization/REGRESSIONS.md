# Engine performance: regressions and failed attempts

Known places where the fork is slower than stock (from
[stock-compare.md](../benchmarks/stock-compare.md)), and attempts that did
not work.

| Date | What | Status |
| --- | --- | --- |
| 2026-11-27 (baseline) | small_1k GPU 0.14 to 0.21 ms (the frame is still faster) | Partly fixed (OD7): with `--gpu-profile` 0.142 stock, 0.181 to 0.173 fork. Left: culling and occlusion dispatches |
| 2026-11-27 (baseline) | Cold shader stall about 10% longer than stock | Fixed in the fork's own code (OD5, OD6: stall -8.5%); not rerun against stock |
| 2026-11-27 (baseline) | scene_bench native at 10k nodes: 0.122 to 0.154 ms | Open (O2) |
| 2026-11-27 | The instance bench's "+144 MB at 1k nodes" is capacity left over from its larger cases, which run in the same process. A fresh 1k-node scene had +11 MB; now +3.4 MB | Explained. Shrinking after an unload: OD9 (300 to 83 MB); 13 MB left, open (O1) |
| 2026-11-27 | Process loop: blind prefetch of +64 and +128 bytes after Node (native 100k +21%), instance ID prefetched for every node (+20-25%) | Dropped; extra lines only for scripted nodes (OD4) |
| 2026-11-27 | native 10k after OD4: 0.145 against stock 0.125 ms; what is left is the virtual notification chain and the Timer state misses | Open (O2). Possibly the toolchain (stock: LTO with another compiler) |
| 2026-11-27 | Editor builds: a `has_gizmos` bit next to `notify_transform`, so the transform walk does not load `data.gizmos` (4.5% self in the script 100k profile) | Dropped: script 100k -4%, script 10k +3%, native mixed, all within spread. The load was the node's first miss, paid by the next access anyway |
| 2026-11-27 | Transform dirty walk: prefetch of the dirty bits, child array and `xform_change` lines of the child two ahead (after OD11) | Dropped: wide 100k +34%, parents 100k +43% (many children per parent, the walk already streams them), deep 100k -0.1 ms (within spread) |
| 2026-11-27 | Scene cull: four chunks per worker, taken as workers finish, against the main thread waiting 14% of city_extreme for the cull (workers 5 to 145 samples each) | Dropped: city within spread, forest and town render CPU +0.03-0.05 ms (more results to clear and merge). The cull is compute bound (12 workers on 6 cores with SMT, no lock contention, about 8 ns per instance); less work is what would help (D037) |
| 2026-11-27 | Render list buffers shrink down to the minimum size (4,096 elements) instead of only from 16,384 up, for the 13 MB a small scene keeps after a large one | Dropped: small_1k after massive_100k 82.9 to 78.7 MB and town 174 to 169 MB, but town render CPU 0.603 to about 0.63 ms over 10 alternated rounds. A temporary log showed each list shrinking once at frame 123 and never growing back, so the cost is not reallocation; not found |
