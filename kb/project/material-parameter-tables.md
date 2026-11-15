---
type: Finding
title: Forward+ material parameter tables
description: One storage-buffer slot per material and shared uniform sets per page and texture set; unique_10k render CPU 7.5-8.1 to 0.42-0.47 ms, draws 8,776 to 11.
tags: [rendering, materials, gpu-driven, draw-calls, forward-plus]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-15T00:00:00+01:00 }
sources:
  - id: decision
    resource: ../../knowledge/TECHNICAL_DECISIONS.md
    title: D036 Material parameter tables for Forward+ scene shaders
  - id: bench
    resource: ../../docs/renderer/benchmarks.md
    title: Instance bench, material parameter tables section
---

# What changed

Forward+ scene shaders read material parameters from a per-shader storage
buffer (`material_table.data[material_slot]`) instead of one UBO per
material. Materials of the same shader, table page (16 to 4,096 slots) and
textures share a uniform set; that group's id is the render sort key's
material id, so the CPU list instances them together and the GPU path puts
them in one bucket. The slot is in `compressed_aabb_position_pad.w` of the
instance data.[^decision]

# Measured

RTX 3060, Vulkan, instance bench p50: `unique_10k` (10,000 materials of one
shader) render CPU 7.5-8.1 to 0.42-0.47 ms, frame 12.8-13.7 to 0.84-0.86
ms, GPU 1.05 to 0.49 ms, draws 8,776 to 11; D3D12 render CPU 0.38-0.41 ms
and 130.7 MiB video memory (734-822 MiB before). Other cases within
spread. Images and motion vectors equal to the CPU path.[^bench]

# Limits

Materials with different textures still draw apart (bindless textures are
the next step). Texture-streaming materials keep their own group. The cost
of storage-buffer reads in heavy fragment shaders was not measured on its
own. Mobile is unchanged.
