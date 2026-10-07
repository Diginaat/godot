# Licenses

This fork combines code and binaries from several sources, each under its own
license. This page lists them all. It covers both the **source code in this
repository** and the **editor builds on the Releases page**.

> **NVIDIA DLSS, Ray Reconstruction, Frame Generation and Reflex runtime
> files are NOT included in this repository or in any release.** They are
> licensed by NVIDIA under terms that don't allow them to be redistributed
> this way. Download them yourself from the
> [NVIDIA Streamline SDK releases](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0)
> (version 2.10.0). Setup instructions are in [README.md](README.md#5-add-the-nvidia-streamline-dlls).
> By downloading them you accept NVIDIA's license terms.

## Summary

| Component | Where | License | License text |
| --- | --- | --- | --- |
| Godot Engine | Whole source tree, editor executables | MIT | [`LICENSE.txt`](LICENSE.txt) |
| Godot's bundled third-party libraries | `thirdparty/` | Various (mostly MIT, BSD, zlib, Apache-2.0) | [`COPYRIGHT.txt`](COPYRIGHT.txt) |
| NVIDIA PhysX 5 SDK and NVIDIA Blast | Linked into the editor; `PhysXGpu_64.dll` in releases | BSD-3-Clause | [`modules/godot_physx/PHYSX-LICENSE.md`](modules/godot_physx/PHYSX-LICENSE.md) |
| PhysX 5 Godot module (`modules/godot_physx`) | Source | MIT | [`LICENSE.txt`](LICENSE.txt) (see the file headers) |
| NVIDIA Streamline SDK headers | `thirdparty/streamline/include` | MIT | [`thirdparty/streamline/LICENSE.txt`](thirdparty/streamline/LICENSE.txt) |
| NVIDIA DLSS / NGX, Reflex, Streamline runtime DLLs | **Not included**, download from NVIDIA | NVIDIA RTX SDKs License, NVIDIA SDK License, MIT (Streamline core) | Shipped inside the Streamline SDK download |
| Microsoft DirectX Agility SDK (`D3D12Core.dll`, `d3d12SDKLayers.dll`) | Release zips only | Microsoft DirectX license (distributable files) | [`misc/dist/licenses/`](misc/dist/licenses/) |
| Mesa NIR (SPIR-V to DXIL, via [godot-nir-static](https://github.com/godotengine/godot-nir-static)) | Linked into the editor | MIT | [Mesa licenses](https://docs.mesa3d.org/license.html) |
| GodotSharp (.NET editor only) | `GodotSharp/` in the .NET release | MIT (part of Godot) | [`LICENSE.txt`](LICENSE.txt) |

## Details

### Godot Engine: MIT

The engine, its editor and this fork's own changes are under the MIT license,
in [`LICENSE.txt`](LICENSE.txt). Copyright (c) 2014-present Godot Engine
contributors, (c) 2007-2014 Juan Linietsky, Ariel Manzur. Third-party code
that Godot itself bundles is listed with its licenses in
[`COPYRIGHT.txt`](COPYRIGHT.txt).

### NVIDIA PhysX 5 and NVIDIA Blast: BSD-3-Clause

The PhysX SDK is not stored in this repository. `build_physx.py` downloads it
from [NVIDIA-Omniverse/PhysX](https://github.com/NVIDIA-Omniverse/PhysX). Its
static libraries are linked into the editor executable, and
`PhysXGpu_64.dll` (the CUDA GPU part) ships next to it in the releases. Both
are under BSD-3-Clause; the full text is in
[`modules/godot_physx/PHYSX-LICENSE.md`](modules/godot_physx/PHYSX-LICENSE.md),
which is also included in every release zip.

### NVIDIA Streamline SDK headers: MIT

The Streamline API headers in `thirdparty/streamline/include` (SDK 2.10.0) are
MIT-licensed by NVIDIA; see
[`thirdparty/streamline/LICENSE.txt`](thirdparty/streamline/LICENSE.txt). That
file also notes that the Nsight Perf parts of the SDK are under the separate
Nsight Perf SDK license. No Nsight Perf files are included here.

### NVIDIA DLSS, Reflex and Streamline runtime files: not included

The editor loads NVIDIA's Streamline runtime (`sl.interposer.dll` and the
`sl.*.dll` plugins) and the DLSS models (`nvngx_dlss.dll`, `nvngx_dlssd.dll`,
`nvngx_dlssg.dll`, `nvngx_deepdvc.dll`) and Reflex (`NvLowLatencyVk.dll`) at
runtime if they're present next to the executable. **None of these files are
in this repository or the releases.** The DLSS and Reflex binaries are licensed
under the NVIDIA RTX SDKs License and the NVIDIA SDK License. Those licenses
allow redistribution only inside an application under conditions this project
can't meet for a standalone open-source download (among them: no open-source
relicensing, required attribution, and notifying NVIDIA before commercial
release).

To use DLSS, download the
[Streamline SDK 2.10.0](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0)
yourself and copy the DLLs from its `bin\x64` folder next to the editor, as the
[README](README.md#5-add-the-nvidia-streamline-dlls) explains. NVIDIA's license
files come with that download and apply to your use of those files. If you ship
a game that uses DLSS, follow NVIDIA's terms, including attribution and the
[software notification](https://developer.nvidia.com/sw-notification) before
commercial release.

Without these files the editor runs normally, but DLSS, Ray Reconstruction,
Frame Generation and Reflex are unavailable. The path tracer still works and
can run without a denoiser.

### Microsoft DirectX Agility SDK: Microsoft DirectX license

The release zips include `D3D12Core.dll` and `d3d12SDKLayers.dll` from the
Microsoft DirectX Agility SDK, which Microsoft lists as distributable files.
Their license text is in
[`misc/dist/licenses/MICROSOFT-DIRECTX-AGILITY-SDK-LICENSE.txt`](misc/dist/licenses/MICROSOFT-DIRECTX-AGILITY-SDK-LICENSE.txt),
and Microsoft's list of distributable files is next to it. Both are included in
every release zip.

## Trademarks

NVIDIA, DLSS, PhysX, GeForce RTX and Reflex are trademarks of NVIDIA
Corporation. Godot and the Godot logo are trademarks of the Godot Foundation.
Microsoft, DirectX and Windows are trademarks of Microsoft Corporation. This
project is not affiliated with, sponsored or endorsed by NVIDIA, the Godot
Foundation or Microsoft.
