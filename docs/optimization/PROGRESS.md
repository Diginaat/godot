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
