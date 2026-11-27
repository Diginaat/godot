# Engine performance: regressions and failed attempts

Known places where the fork is slower than stock (from
[stock-compare.md](../benchmarks/stock-compare.md)), and attempts that did
not work.

| Date | What | Status |
| --- | --- | --- |
| 2026-11-27 (baseline) | small_1k GPU 0.14 to 0.21 ms (the frame is still faster) | Open (O3) |
| 2026-11-27 (baseline) | Cold shader stall about 10% longer than stock | Open (O4) |
| 2026-11-27 (baseline) | scene_bench native at 10k nodes: 0.122 to 0.154 ms | Open (O2) |
| 2026-11-27 | The instance bench's "+144 MB at 1k nodes" is capacity left over from its larger cases, which run in the same process. A fresh 1k-node scene had +11 MB; now +3.4 MB | Explained. Shrinking after an unload is still open (O1) |
