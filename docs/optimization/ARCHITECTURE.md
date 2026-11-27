# Engine performance: architecture notes

Where the measured costs live. Design documents of the systems this brief
touches:

- GPU-driven geometry (instance table, GPU instance path, HZB, MultiMesh
  culling, cluster LOD): [docs/renderer/MEMORY.md](../renderer/MEMORY.md)
  and its linked audit.
- Scene runtime (groups, children cache, transform propagation, process
  loop): [docs/scene-runtime/MEMORY.md](../scene-runtime/MEMORY.md).
- Shader variants and pipelines:
  [docs/shader-permutations/memory.md](../shader-permutations/memory.md).

## GPU memory of the GPU-driven systems (O1)

Measured per feature at 200k nodes (city_extreme, 1280x720), by turning
each feature off:

- Instance table, with what it enables: 133 MB.
- GPU instance path: 123 MB.
- Occlusion: 35 MB.

The largest single buffers (named for RenderDoc since 2026-11-27):

| Buffer | Size per element | Elements | At 200k nodes |
| --- | --- | --- | --- |
| GPU Instances Instance Data (removed by OD10) | 176 B (`SceneState::InstanceData`) | one per member (instance x surface), written every frame | 55 MB |
| GPU Instance Table | 96 B (`GPUInstanceTable::Record`; 160 B before OD12, 112 B before OD13) | one per slot | 21 MB |
| GPU Instance Previous Transforms (OD12) | 48 B (`GPUInstancePrevTable::Entry`) | one per instance that moved last frame | 12 KB to a few MB |
| GPU Instances Members | 16 B | one per member | 5 MB |
| IDs, phase 1/2 IDs, shadow IDs | 4 B | one per member, per pass list | 2.4-4.9 MB each |
| HZB | 4 B (R32F) with mips | half the internal resolution, power-of-two padded | 2.7 MB at 720p, 5.3 MB at 1080p |

The per-member instance data differs per surface (compressed mesh AABB,
material table slot, UV scale), so it cannot be shared per instance. Since
OD10 it is not stored: the scene shader builds it from the table record,
the member and its bucket (`gpu_instance_data()`).
