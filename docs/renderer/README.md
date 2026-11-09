# GPU-first renderer docs

Planning and records for moving Forward+ toward GPU-driven rendering. Start
with [MEMORY.md](MEMORY.md) (current state and next step).

| File | Contents |
| --- | --- |
| [MEMORY.md](MEMORY.md) | Session entry point: branch, phase, active task, next step |
| [architecture.md](architecture.md) | Phase 0 audit: frame pipeline, threads, CPU/GPU split, code paths |
| [compatibility.md](compatibility.md) | Backend capability matrix and fallback rules |
| [roadmap.md](roadmap.md) | Dependency-ordered phases with tasks and acceptance criteria |
| [benchmarks.md](benchmarks.md) | Baseline and milestone measurements |
| [testing.md](testing.md) | Gates per milestone, correctness cases, bench protocol |
| [research.md](research.md) | Sources and techniques, and what they mean for this fork |

Decisions are numbered with the geometry work's in
[knowledge/TECHNICAL_DECISIONS.md](../../knowledge/TECHNICAL_DECISIONS.md)
(D025 onward), so there is one decision log.
