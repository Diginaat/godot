# Engine performance: progress log

## 2026-11-27

- Brief started after the renderer roadmap was finished. Baseline:
  [stock-compare.md](../benchmarks/stock-compare.md).
- O1: VRAM attributed per feature, then per buffer. Allocations over 1 MB
  were logged temporarily, and the GPU instance buffers now carry names.
- O1, OD1 and OD2: growth with an eighth of headroom, and the HZB at half
  resolution. VRAM:
  - city_extreme 475 to 415 MB
  - forest 237 to 221 MB
  - town 201 to 192 MB
  - a 1k-node scene 80.2 to 72.2 MB (stock 68.8)

  GPU time is slightly lower.
- O2 and O5: sampling profiles of the script and native scenarios
  (debug-symbol build). OD3 (no allocation per GDScript notification) and
  OD4 (process loop prefetch): script 100k 36.1 to 23.6 ms, script 10k
  1.486 to 1.141 ms. Native 10k is still 0.02 ms behind stock; two
  prefetch variants that slowed it were dropped (REGRESSIONS.md).
- O4: the cold stall waits for ubershader pipelines. Two fork additions
  were compiled into all of them: OD5 (instance data loaded once) and OD6
  (DDGI probe capture as a pipeline constant). Stall 48.4 to 44.3 s, same
  images (forced-ubershader comparison against specialized pipelines,
  with a negative control).
- O3: small_1k GPU time per pass against stock; the gap is the GPU paths'
  fixed costs. OD7 (HZB four levels per dispatch) took 0.008 ms off it.
- O5: native property get and set through validated calls (OD8), script
  processing 100k 23.6 to 22.1 ms.
- O1: VRAM after a large scene is unloaded (OD9). Slots, GPU instance
  buffers, CPU list instance buffers, the upload staging ring and the
  transfer worker staging buffers shrink. small_1k after massive_100k
  300 to 83 MB (fresh 70), massive_100k 244 to 136 MB, level bench
  city_extreme 415 to 287 MB, forest 221 to 165 MB.
- O1: the scene shader builds the GPU path's instance data (OD10): no
  176-byte copy per member. city_extreme 287 to 232 MB, forest 165 to
  149 MB (stock 158), massive_100k GPU 0.948 to 0.903 ms.
- O2: reparent (1.05x stock) and deep (1.08x) measured; deep profiled.
  OD11: global transforms of dirty chains in a loop, deep 100k 18.7 to
  15.1 ms (stock 18.2). Reparent is already O(1) per call (1.3 us at
  100k); nothing found to take.
- O1: previous transforms out of the instance table record (OD12): 160
  to 112 bytes per slot, a compacted side table for instances that moved.
  city_extreme 232 to 222 MB (stock 188), churn_100k 131.5 to 120.3 MB.
  The first version had a span leak (+5% GPU in dynamic_20k after another
  case), fixed by compaction before commit. A gizmo flag for the transform walk was tried and
  dropped (REGRESSIONS.md).
