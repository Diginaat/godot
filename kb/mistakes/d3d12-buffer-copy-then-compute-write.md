---
type: Mistake
title: GPU buffer copy followed by compute writes broke D3D12
description: Seeding a command buffer with RD buffer_copy and then writing it from a compute list made D3D12 command lists fail to close and removed the device; Vulkan was fine.
tags: [mistake, d3d12, rendering-device, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-06T23:59:00+01:00 }
---

# What happened

MultiMesh HZB occlusion seeded its second indirect command buffer with
`RD::buffer_copy(commands, occluded_commands, ...)` and then let the scan
kernel rewrite the instance counts in that buffer. On Vulkan the fixture
passed. On D3D12 every frame logged `Close failed with error 0x80070057`
(`command_buffer_end`), then `0x887a0005` (device removed). `--gpu-validation`
printed nothing more.

A bisect with a temporary environment switch (one build, several runs)
showed that the copy alone and the compute writes alone both worked; only the
two together failed.

# Rule

For a buffer that a compute list writes afterwards, upload its initial
contents with `buffer_update` from CPU data (the pattern the culling kernels
already use) instead of a GPU `buffer_copy`. Run every new GPU path on D3D12
as well as Vulkan before calling it done. When validation is silent,
bisect with runtime switches in one build instead of one rebuild per guess.
