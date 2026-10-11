---
type: Reference
title: Gaussian compression for precomputed indirect illumination
description: Compresses baked SH probes with 3D Gaussians carrying latent codes plus a small MLP decoder; cascaded lighting volumes at runtime.
tags: [research, probes, compression, baking]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: paper
    resource: https://doi.org/10.1145/3721238.3730758
    title: Zhou et al., Gaussian Compression for Precomputed Indirect Illumination, SIGGRAPH 2025 Conference Papers
---

# Summary

- Probe data (SH coefficients, 3/12/27 values) is fitted by K Gaussians
  (5-20% of the probe count) with 3DGS-style parameters and a latent code;
  a probe's latent is the Gaussian-weighted sum, decoded with its position
  by a two-layer MLP (64 wide). Trained by gradient descent (custom CUDA),
  then quantized to 10 bits with fine-tuning.[^paper]
- About 1:50 compression; PSNR 42.9 at 2.4% size vs 39.1 for clustered PCA.
  Compression takes 1-2 minutes per scene of about 15,000 probes.
- Runtime: **cascaded lighting volume**: 4 camera-centred volumes (2, 4, 8,
  32 m blocks, 16^3 each), scrolled, blocks rebuilt from nearby probes (up
  to 64 blocks per frame, LRU cache); decompression 0.3-1.4 ms per frame.
- Static scenes only (precomputed).

# For this fork

Low priority. Our baked `DDGIProbeData` is octahedral atlases (3.6 MB for
the interior test scene), not SH, and DDGI is mainly dynamic. If baked data
size becomes a problem, simpler first steps are BC6H texture compression
of the atlases or L0-relative storage. See
[compressed baked data](/ddgi/improvements/compressed-baked-data.md).

[^paper]: Zhou et al., SIGGRAPH 2025
