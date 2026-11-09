# Backend capability matrix

What RenderingDevice and its drivers expose today, from the source
(2026-11-10, `4bc2a22774`). "Driver only" means the driver implements it but
no RD API reaches it. Compatibility (GLES3) has no RenderingDevice and keeps
its own renderer; nothing in this project changes it.

| Feature | Vulkan | D3D12 | Metal | Notes |
| --- | --- | --- | --- | --- |
| Single draw indirect | yes | yes | yes | `draw_list_draw_indirect` |
| Multi-draw indirect (`SUPPORTS_MULTI_DRAW_INDIRECT`, fork) | if `multiDrawIndirect` | yes | no (loops one draw per record) | D3D12 ignores `p_stride`: only 20 (indexed) / 16 byte records work |
| Draw indirect with GPU count | driver only | driver only | not implemented | Vulkan does not enable `drawIndirectCount`; RD API missing |
| Dispatch indirect | yes | yes (`ExecuteIndirect`) | yes (unverified) | one dispatch per call |
| `drawIndirectFirstInstance` | enabled when present | n/a (always) | ? | needed for per-draw instance offsets in MDI |
| Mesh shaders | no | no | no | RD pipeline create says "not implemented yet" |
| Buffer device address | optional | conditional | conditional | |
| Descriptor indexing (bindless arrays) | partial (no update-after-bind) | ? | ? | `renderer_rd/bindless_block.cpp` |
| Ray query / RT pipeline | optional (+ SER) | no (stubs) | no | path tracer and DDGI need Vulkan |
| Subgroup operations | yes (limits) | yes (wave ops) | ? | |
| 64-bit atomics | no | ? | ? | rules out 64-bit visibility-buffer tricks for now |
| Async compute queue | no | no | no | one main queue for graphics and compute |
| Transfer queue | init data only | init data only (COPY list) | ? | |
| Persistently mapped dynamic buffers | yes | yes | ? | `BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT`, C++ only |
| GPU-mappable device memory | yes (`SUPPORTS_GPU_MAPPABLE_BUFFER`) | UMA or GPU upload heap | ? | |
| Timestamps | yes | yes | ? | |
| Memory budget | VMA | D3D12MA | ? | `get_memory_usage` |

Consequences for the roadmap:

- GPU-driven submission can rely on single and multi-draw indirect on Vulkan
  and D3D12. Metal must keep one draw per record (correct, slower).
- A GPU-written draw count needs a new RD call plus the Vulkan feature. Until
  then, compacted command lists use a fixed count with zero-instance records
  (what CLOD does now).
- Mesh shaders, async compute and 64-bit atomics are not available; the
  design must not depend on them.

## Fallback rules

- Every GPU path is capability-gated and opt-in until it has a benchmark win
  and full fixtures; the conventional CPU path stays the reference.
- Unsupported cases stay on the CPU path: skinned/blend-shape meshes, custom
  vertex transforms, alpha/transparent, multiple material passes, XR
  multiview, mirrors/reflection probes, double-precision builds (current
  rules in `multimesh_culling.cpp` and `mesh_clod.cpp`).
- Mobile and Compatibility renderers keep their behavior.
