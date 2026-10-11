---
type: Overview
title: Project overview
description: A Godot 4 engine fork combining upstream Godot, NVIDIA's DLSS and path tracer, and a PhysX 5 module.
tags: [project, branches, workflow]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: custom-build
    resource: ../../CUSTOM_BUILD.md
    title: CUSTOM_BUILD.md (runbook)
---

# What it is

A Godot 4 (master) engine build that combines three sources: official
Godot, NVIDIA-RTX's branch (DLSS through Streamline, ray reconstruction,
path tracer), and a PhysX 5 module (GPU dynamics, Blast, Flow). It adds its
own features on top: DDGI probe global illumination, a native path tracer
denoiser (in progress), and XeGTAO ambient occlusion (in progress). The
procedures are in [CUSTOM_BUILD.md](../../CUSTOM_BUILD.md).[^custom-build]

# Branches

| Branch | Purpose |
| --- | --- |
| `nvidia-pt-dlss` | Published (as `nvidia-dlss-physx` on the fork); only tested work |
| `dev` | Integration of finished work |
| `dev-pt` | Path tracer work |
| `dev-ddgi` | DDGI work |
| `dev-ddgi-ao` | XeGTAO ambient occlusion (see [XeGTAO](/xegtao/overview.md)) |
| `dev-rrdenoiser` | Native path tracer denoiser |

Release flow: feature branch, then `dev`, then `nvidia-pt-dlss`, fast
forward only. Never push to the NVIDIA remote.

# Areas

* [DDGI](/ddgi/overview.md)
* [XeGTAO](/xegtao/overview.md)

[^custom-build]: CUSTOM_BUILD.md (runbook)
