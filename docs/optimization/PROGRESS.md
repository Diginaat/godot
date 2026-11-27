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
