# Research

Papers and talks read, summarized for this project. Implement from the
ideas; check the license before taking any code.

# Global illumination (read 2026-10-12)

* [SDFDDGI](/research/sdfddgi.md) - DDGI with SDF scene tracing, SDF probe relocation, SDF visibility, Contact GI on GTAO
* [Enemies: adaptive probe volumes](/research/enemies-adaptive-probe-volumes.md) - Unity: SDF-guided brick subdivision, validity mask weighting, virtual offset, dilation, seam dithering
* [Ghost of Yotei ambient lighting](/research/ghost-of-yotei-ambient.md) - octree probes, interior masking, relit octahedral G-buffer probes, screen-space RT probes
* [Enlisted scalable GI](/research/enlisted-scalable-gi.md) - voxel scene clipmap with G-buffer feedback, probe update queues, convex interior filtering
* [Adaptive placement of GI probes (Forza)](/research/forza-adaptive-probe-placement.md) - procedural layered probe point clouds for baking
* [Gaussian compression for probes](/research/gaussian-probe-compression.md) - Gaussian + MLP compression of baked SH probes, cascaded lighting volumes

# Probes, baking and reconstruction (read 2026-10-12, second set)

* [Unity 6 Adaptive Probe Volumes](/research/unity6-adaptive-probe-volumes.md) - 4x4x4 bricks at 1/3/9/27 m, baked sky occlusion x dynamic sky color, scenario blending, streaming
* [Tetrahedral probe interpolation (GDC 2012)](/research/tetrahedral-probe-interpolation.md) - Delaunay tets, barycentric weights, coherent walks
* [CoD: Infinite Warfare precomputed lighting](/research/cod-iw-precomputed-lighting.md) - decoupled self-visibility, regular tet light grid with per-probe visibility, SH de-ringing, least-squares baking
* [Stupid SH Tricks](/research/stupid-sh-tricks.md) - SH irradiance, ringing and windowing, lights from SH
* [GI downscaling](/research/gi-downscaling.md) - GI at 1/4-1/32 resolution: downsampled G-buffer, depth-normal upsampling, L1 SH
* [FF7 Rebirth GI analysis](/research/ff7-rebirth-gi.md) - ambient cube probes, reflection leaks, specular occlusion fixes
* [Zhihu article 571673961](/research/zhihu-571673961.md) - not read: access refused

# DDGI sources and engines (read 2026-10-12)

* [DDGI papers and reference code (Majercik 2019, 2021)](/research/majercik-ddgi-reference.md) - our sampling matches the reference; it averages in gamma 5 space and floors visibility at 0.05
* [O3DE DiffuseProbeGrid](/research/o3de-diffuse-probe-grid.md) - RTXGI in O3DE: thresholds, emissive multiplier, a simpler low resolution composite; no code taken

# What they mean for DDGI

* [DDGI improvement proposals, ranked](/ddgi/improvements/index.md)
