---
type: Mistake
title: SDK build outside the Visual Studio developer shell
description: The PhysX GPU preset (Ninja) needs cl and ninja on PATH, and CUDA 12.8 rejects VS 2026's own toolset; build the SDKs inside vcvars64 -vcvars_ver=14.44.
tags: [mistake, build, windows, physx, cuda]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What happened

`build_physx.py --gpu --blast --flow` from a plain shell failed:
`CMake was unable to find a build program corresponding to "Ninja
Multi-Config"`. Inside the default VS 2026 developer shell, nvcc 12.8
failed with `unsupported Microsoft Visual Studio version!` (v145). Blast's
`repo` tool then needed network for packman and PyPI (`uv.exe`), which a
firewall blocked with `WinError 10013`. The setup script also stopped at
its `python -m SCons --version *> $null` check when run with output
redirected under `$ErrorActionPreference = "Stop"` on PowerShell 5.1.

# Rule

* Build the SDKs from `vcvars64.bat -vcvars_ver=14.44` (VS 2026's MSVC v143
  component), with vswhere on PATH, and allow network for packman and uv.
* If `custom.py` is missing but `physx-sdk` exists, write `custom.py` by
  hand (see `setup_dev_windows.ps1` step 5) instead of rebuilding.
