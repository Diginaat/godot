# Godot Engine with NVIDIA DLSS, path tracing and PhysX 5

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="300" alt="Godot Engine logo">
  </a>
</p>

This is a fork of [Godot Engine](https://github.com/godotengine/godot) (`master`,
4.8-dev) that combines three things in one source tree:

| Part | Source | What it adds |
| --- | --- | --- |
| Godot Engine | [godotengine/godot](https://github.com/godotengine/godot) `master` | The engine itself, kept up to date with upstream. |
| NVIDIA RTX branch | [NVIDIA-RTX/godot](https://github.com/NVIDIA-RTX/godot) `nvidia-pt-dlss` | DLSS through NVIDIA Streamline, a real-time path tracer with DLSS Ray Reconstruction, Reflex, and Nsight Aftermath crash dumps. |
| PhysX 5 module | [uno1982/godot](https://github.com/uno1982/godot/tree/feature/physx5-module) `feature/physx5-module` | `modules/godot_physx`: an NVIDIA PhysX 5 physics server with CUDA GPU dynamics, plus GPU fluid, cloth, soft bodies, destruction, vehicles and water. |

All credit for those features goes to their authors: the Godot contributors,
NVIDIA's RTX team, and the PhysX module's author. This fork merges them,
fixes the conflicts between them, and keeps them building against the latest
Godot `master`.

## PhysX module by Wild Ox Studios

<p>
  <a href="https://www.youtube.com/@WildOxStudios">
    <img src="https://yt3.googleusercontent.com/ytc/AIdro_mRiIqNDV8Cx8l-2ze-wKUb9tczLXpsajPuE37jikCundo=s160-c-k-c0x00ffffff-no-rj" width="80" height="80" align="left" alt="Wild Ox Studios logo">
  </a>
</p>

The PhysX 5 module (`modules/godot_physx`) is made by **Wild Ox Studios**
([uno1982](https://github.com/uno1982) on GitHub). This fork only vendors and
adapts it; all the PhysX work is theirs.

- **YouTube:** [Wild Ox Studios](https://www.youtube.com/@WildOxStudios), for
  videos of the module in action.
- **Module source:** [uno1982/godot](https://github.com/uno1982/godot/tree/feature/physx5-module) (`feature/physx5-module`)
- **Example project:** [uno1982/godot-physx-example](https://github.com/uno1982/godot-physx-example),
  demo scenes and benchmarks for the module. See [Try the PhysX example project](#try-the-physx-example-project).

<br clear="left">

## Downloads

Prebuilt Windows editors (standard and .NET) are on the
[**Releases page**](https://github.com/Diginaat/godot/releases).
Each release names the Godot version it's based on and this build's own
version: `godot4.8-dev-nvidia-rt-dlss-physx-v0.2.0` is build 0.2.0, on Godot
4.8-dev.

> [!IMPORTANT]
> **The downloads work out of the box. You only need the NVIDIA Streamline SDK
> if you want DLSS.**
>
> NVIDIA DLSS is not included in this repository or in the release downloads,
> because NVIDIA's license doesn't allow redistributing the DLSS, Ray
> Reconstruction, Frame Generation and Reflex runtime files this way.
> The editor, the path tracer and PhysX GPU all work without them.
>
> **Only if you want DLSS** (or Ray Reconstruction, Frame Generation or Reflex),
> either:
>
> - click **Get NVIDIA DLSS...** in the editor's menu bar (after Help). It
>   explains each step, asks you to accept NVIDIA's license terms and to confirm
>   the download from GitHub, checks the file's SHA-256, installs the DLLs next
>   to the editor and offers to restart; or
> - download the [NVIDIA Streamline SDK 2.10.0](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0)
>   yourself and copy its DLLs next to the editor ([step 5 below](#5-optional-add-the-nvidia-streamline-dlls-only-for-dlss)).

**Platform:** Windows 10/11 x64 with an NVIDIA RTX GPU. DLSS, Ray
Reconstruction and the path tracer need an RTX card. PhysX GPU dynamics need an
NVIDIA GPU with CUDA. Everything else falls back gracefully (PhysX runs on the
CPU, rendering without DLSS).

## What's inside

### NVIDIA DLSS, path tracing and Streamline (from NVIDIA-RTX/godot)

- **DLSS Super Resolution** as a 3D scaling mode (Project Settings >
  Rendering > Scaling 3D > Mode = DLSS), with selectable presets
  (`rendering/streamline/dlss_preset`).
- **Real-time path tracer** in the Forward+ renderer: enable it per
  `Environment` (`pathtracing_enabled`, samples per pixel, max bounces, debug
  views). It uses hardware ray tracing pipelines, so it needs the **Vulkan**
  driver (Godot's D3D12 driver has no ray tracing).
- **DLSS Ray Reconstruction** as the path tracer's denoiser
  (`Environment.pathtracing_denoiser = 1`).
- **NVIDIA Reflex** low-latency modes (`rendering/streamline/reflex_mode`).
- Streamline plugins for NIS, DeepDVC and DLSS Frame Generation are loaded when
  their DLLs are present.
- **Nsight Aftermath** GPU crash dumps (`use_aftermath=yes` at build time),
  plus `--gpu-markers`, `--debug-shaders` and `--raytracing-validation`
  command-line flags.

### PhysX 5 (from the godot_physx module)

Select it in Project Settings > Physics > 3D > Physics Engine = `PhysX`. Every
standard 3D physics node keeps working. On top of that:

- GPU rigid-body dynamics on NVIDIA GPUs (CUDA), with automatic CPU fallback.
- `PhysXParticleFluid3D` (GPU fluid with surface meshing), `PhysXGranular3D`
  (sand and snow), `PhysXGas3D` (smoke and fire), `PhysXCloth3D`, GPU soft
  bodies for the stock `SoftBody3D`, `PhysXChunkEmitter3D` debris.
- `PhysXDestructible3D`: runtime mesh fracture with NVIDIA Blast (optional
  SDK), plus an in-editor fracture tool.
- Vehicles (`PhysXVehicle3D`, `PhysXMotorcycle3D`, `PhysXTank3D`), water
  (`PhysXWaterSurface3D`, FFT ocean, caustics) and boats.

The module's full documentation is in
[`modules/godot_physx/README.md`](modules/godot_physx/README.md).

#### Try the PhysX example project

The module's author, [Wild Ox Studios](https://www.youtube.com/@WildOxStudios),
publishes a demo and benchmark project:
**[uno1982/godot-physx-example](https://github.com/uno1982/godot-physx-example)**.
It only works with an editor that includes the PhysX module, such as this one.

1. Download or clone it:
   ```
   git clone https://github.com/uno1982/godot-physx-example.git
   ```
2. Open its `project.godot` with this editor. PhysX is already selected as the
   physics engine.
3. Pick a scene by what it needs:
   - `cpu/`: rigid bodies, joints, characters, areas, queries. Works everywhere.
   - `gpu/`: GPU particle fluids. Needs an NVIDIA GPU with CUDA; the release
     editors are built with `physx_gpu=yes`.
   - `flow/`: NVIDIA Flow smoke, fire and dust. Needs an editor built with
     `flow_sdk=...`, which the release editors aren't yet. The scenes still
     open, with the Flow nodes as placeholders.

The log shows `PhysX 5.10.0 initialized [GPU]` when GPU dynamics are active.

### Changes in this fork

- Merged with the latest Godot `master`, with every conflict resolved by hand
  (both sides kept).
- Shader container format bumped to version 5, so shader caches from older
  builds are rejected instead of crashing D3D12 startup.
- New projects default to the Vulkan driver on Windows, because the path
  tracer needs it. Requesting path tracing on D3D12 prints an explanatory
  warning.
- `misc/scripts/package_editor_win64.ps1` packages a release editor with all
  runtime DLLs.
- [`CUSTOM_BUILD.md`](CUSTOM_BUILD.md) is the maintenance runbook for keeping
  the three sources in sync.

## Building on Windows

### 1. Prerequisites

- **Visual Studio 2022** (or Build Tools) with the "Desktop development with
  C++" workload and a Windows SDK.
- **Python 3** and **SCons**: `python -m pip install scons`.
- **Git** and **CMake** (the PhysX SDK builds with CMake).
- **CUDA Toolkit 12.8**, only for PhysX GPU dynamics:
  `winget install Nvidia.CUDA --version 12.8`. Open a new terminal afterwards
  so `CUDA_PATH` is set.
- **.NET SDK 8 or newer**, only for the C# (.NET) editor.
- The **D3D12 build dependencies** (Mesa NIR and the Agility SDK), once:
  ```
  python misc/scripts/install_d3d12_sdk_windows.py
  ```

### 2. Get the source

```
git clone -b nvidia-dlss-physx https://github.com/Diginaat/godot.git godot-rtx
cd godot-rtx
```

### 3. Build the PhysX SDK

The PhysX SDK is not included. A script clones NVIDIA's PhysX repository at a
pinned version, applies the Godot build preset and patches, and builds it:

```
python modules/godot_physx/misc/build_physx.py --gpu
```

Leave out `--gpu` for a CPU-only build, and add `--blast` to also build the
NVIDIA Blast SDK for destruction. The script prints the `physx_sdk=` (and
`blast_sdk=`) path to use in the next step. It clones into a `physx-sdk`
folder next to this repository.

### 4. Build the editor

```
python -m SCons platform=windows target=editor production=yes physx_sdk=<path from step 3> physx_gpu=yes
```

- Add `blast_sdk=<path>` if you built Blast.
- Leave out `physx_gpu=yes` for a CPU-only PhysX build. Leave out `physx_sdk`
  completely to build without PhysX.
- The editor lands in `bin\godot.windows.editor.x86_64.exe`.
  `PhysXGpu_64.dll` is copied next to it automatically.

### 5. Optional: add the NVIDIA Streamline DLLs (only for DLSS)

**Skip this step unless you want DLSS, Ray Reconstruction, Frame Generation or
Reflex.** Everything else works without it.

Streamline's runtime DLLs are not in this repository; NVIDIA distributes them
in the Streamline SDK. This source is built against **Streamline SDK 2.10.0**
(see `thirdparty/streamline/include/sl_version.h`).

The easy way: in the editor, click **Get NVIDIA DLSS...** in the menu bar and
follow the dialog. It does the steps below for you. To do them by hand:

1. Download **Streamline SDK 2.10.0** from NVIDIA:
   [github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0).
   Product page: [developer.nvidia.com/rtx/streamline](https://developer.nvidia.com/rtx/streamline).
2. From the SDK's `bin\x64` folder, copy these files into this repository's `bin\`
   folder, next to the editor:
   - `sl.interposer.dll` and the other `sl.*.dll` plugins,
   - `nvngx_dlss.dll` (DLSS), `nvngx_dlssd.dll` (Ray Reconstruction),
     `nvngx_dlssg.dll` (Frame Generation), `nvngx_deepdvc.dll`,
   - `NvLowLatencyVk.dll` (Reflex on Vulkan),
   - the license files (`nvngx_dlss.license.txt`, `reflex.license.txt`,
     `nis.license.txt`).
3. Use the **release** DLLs from `bin\x64`, not `bin\x64\development`. The
   development DLLs are unsigned debug builds with an on-screen overlay. They're
   for debugging only and must not be shipped.

Without these DLLs the editor runs normally, including the path tracer and
PhysX GPU. Only DLSS, Ray Reconstruction, Frame Generation and Reflex are
unavailable.

### 6. Optional: C# (.NET) editor

```
python -m SCons platform=windows target=editor production=yes module_mono_enabled=yes physx_sdk=<path> physx_gpu=yes
bin\godot.windows.editor.x86_64.mono.console.exe --headless --generate-mono-glue modules\mono\glue
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
```

This produces `bin\godot.windows.editor.x86_64.mono.exe` and the
`bin\GodotSharp` folder with the C# API assemblies.

### 7. Optional: package a distributable zip

```
powershell -File misc/scripts/package_editor_win64.ps1          # standard editor
powershell -File misc/scripts/package_editor_win64.ps1 -Mono    # .NET editor
```

The zip goes to `dist\`. It contains the editor, `PhysXGpu_64.dll`, the D3D12
Agility SDK DLLs, all license files, a notice explaining where to get NVIDIA
DLSS and, for .NET, the `GodotSharp` folder. It does **not** include NVIDIA's
Streamline/DLSS runtime files, so it's safe to share. `-WithNvidiaRuntime`
adds them for your own machines only; don't publish that zip (see
[`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md)).

## Using the features

- **DLSS** (needs the optional Streamline DLLs from step 5): Project Settings >
  Rendering > Scaling 3D > Mode = DLSS. Pick a scale, for example 0.67 for
  Quality.
- **Path tracing:** use the Forward+ renderer with the Vulkan driver, add a
  `WorldEnvironment`, and in its `Environment` enable **Pathtracing**. In
  GDScript:
  ```gdscript
  var env: Environment = $WorldEnvironment.environment
  env.pathtracing_enabled = true
  env.pathtracing_samples_per_pixel = 1
  env.pathtracing_max_bounces = 2
  env.pathtracing_denoiser = 1  # DLSS Ray Reconstruction (needs step 5); 0 = none
  ```
- **PhysX:** Project Settings > Physics > 3D > Physics Engine = PhysX. GPU
  dynamics start automatically on a CUDA-capable GPU; the log shows
  `PhysX ... initialized [GPU]`. For ready-made scenes, open the
  [PhysX example project](#try-the-physx-example-project).

## Keeping it up to date

The fork tracks three upstreams. The update procedure, conflict rules and smoke
tests are in [`CUSTOM_BUILD.md`](CUSTOM_BUILD.md). Its tables and examples use
the maintainer's own local paths; replace them with yours.

## Licenses

All components and their licenses are listed in
[**`THIRD_PARTY_LICENSES.md`**](THIRD_PARTY_LICENSES.md). In short:

- Godot Engine and this fork's changes: MIT, see [`LICENSE.txt`](LICENSE.txt)
  and [`COPYRIGHT.txt`](COPYRIGHT.txt).
- NVIDIA PhysX and Blast: BSD-3-Clause, see
  [`modules/godot_physx/PHYSX-LICENSE.md`](modules/godot_physx/PHYSX-LICENSE.md).
- NVIDIA Streamline SDK headers: MIT, see
  [`thirdparty/streamline/LICENSE.txt`](thirdparty/streamline/LICENSE.txt).
- NVIDIA DLSS, Reflex and the Streamline runtime DLLs: **not included**.
  They're under NVIDIA's own license terms, which come with the
  [SDK download](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0).
- Microsoft DirectX Agility SDK (`D3D12Core.dll`, `d3d12SDKLayers.dll`, in the
  release zips only): Microsoft DirectX license, see
  [`misc/dist/licenses/`](misc/dist/licenses/).

This fork is not affiliated with or endorsed by the Godot Foundation or
NVIDIA. For general Godot documentation, see
[docs.godotengine.org](https://docs.godotengine.org), and for the upstream
project README, see
[godotengine/godot](https://github.com/godotengine/godot#readme).
