# Custom Godot 4 build: upstream + NVIDIA DLSS/path tracer + PhysX 5

This repository is a personal Godot 4 engine build that combines three sources
and must be kept up to date with all three. This file is the maintenance runbook
for humans and coding agents.

## Sources

| Remote | URL | Branch | What it provides |
| --- | --- | --- | --- |
| `upstream` | https://github.com/godotengine/godot | `master` | Official Godot 4 engine (the base everything tracks) |
| `origin` | https://github.com/NVIDIA-RTX/godot | `nvidia-pt-dlss` | DLSS (Streamline), Ray Reconstruction, path tracer, Aftermath |
| `physx` | https://github.com/uno1982/godot | `feature/physx5-module` | `modules/godot_physx/` (PhysX 5, GPU dynamics, Blast, Flow, water, vehicles) |

Working branch: `nvidia-pt-dlss` (local). Published to the public fork
**https://github.com/Diginaat/godot**, branch `nvidia-dlss-physx` (the fork's
default branch), via the `fork` remote. Never push to `origin` (NVIDIA-RTX).

New work goes on the `dev` branch first (also pushed to the fork as `dev`).
Merge it into `nvidia-pt-dlss` only after the build and smoke tests pass, so
the published branch always works. After publishing, fast-forward `dev` to
`nvidia-pt-dlss` and push both:
`git push fork nvidia-pt-dlss:nvidia-dlss-physx dev:dev`.

If a remote is missing:

```
git remote add upstream https://github.com/godotengine/godot.git
git remote add physx https://github.com/uno1982/godot.git
git remote add fork https://github.com/Diginaat/godot.git
```

## How the three sources are combined

- **Upstream and NVIDIA are merged as git history.** The NVIDIA branch is a few
  commits on top of upstream master (`NVIDIA: Miscellaneous`, `NVIDIA: Dependencies`,
  `NVIDIA: Pathtracer + DLSS`). Merge `upstream/master` and `origin/nvidia-pt-dlss` into
  the working branch.
- **PhysX is vendored as a directory, NOT merged.** The `physx` branch is based on
  `4.7-stable`, not master. Merging it would pull hundreds of 4.7 release backports into
  a master-based tree. Only `modules/godot_physx/` is taken from it. Its commits touch no
  other engine file that matters for PhysX.
- `modules/godot_physx/` carries local changes on top of the vendored copy:
  adaptations to upstream master API changes, plus the `generator="ninja"` line in
  `misc/physx_presets/vc17win64-godot-gpu.xml`. Never overwrite the directory
  wholesale; merge 3-way (see below).

### Sync state log

Update this table after every sync.

| Date | upstream/master | origin (NVIDIA) | physx module commit | Merge commit |
| --- | --- | --- | --- | --- |
| 2026-10-07 | `e7b12e7492` | `135dff3887` | `2de7ea521f` (Flow builds pipelines on first use) | `2424aa594a` |
| 2026-10-08 | `e7b12e7492` (no change) | `135dff3887` (no change) | `5681d7519a` (Flow on Linux, MIT license) | on `dev`, see log |

## Update procedure

1. **Start clean.** `git status` must be empty. Commit or stash first, and save a patch:
   `git diff --binary HEAD > <scratch>/wip.patch`.
2. **Fetch:** `git fetch upstream master; git fetch origin; git fetch physx feature/physx5-module`.
3. **See what's new:**
   - `git rev-list --count HEAD..upstream/master`
   - `git rev-list --count HEAD..origin/nvidia-pt-dlss`
   - `git log --oneline <last physx commit>..physx/feature/physx5-module -- modules/godot_physx`
4. **Back up:** `git branch backup/pre-sync-<date> HEAD`.
5. **Merge upstream:** `git merge --no-ff upstream/master`.
6. **Merge NVIDIA** if it has new commits: `git merge --no-ff origin/nvidia-pt-dlss`.
7. **Update PhysX** if it has new module commits, 3-way, keeping local adaptations:
   ```
   git diff <last physx commit> physx/feature/physx5-module -- modules/godot_physx > physx.patch
   git apply --3way physx.patch
   ```
8. **Build with `-k`** and fix all errors in one pass (see Build).
9. **Smoke test** (see Test). Both must pass before committing.
10. **Commit,** then update the sync state log above.
11. **Publish:** `git push fork nvidia-pt-dlss:nvidia-dlss-physx`. Before pushing,
    check that no private paths or project names are in tracked files
    (`git grep`); the fork is public.

### Conflict rules (learned the hard way)

- **Never resolve a conflicted file by taking one side wholesale.** The first merge on
  2026-10-07 did that and silently dropped upstream changes in 13 files and fork changes
  in 5. Use `git checkout --conflict=zdiff3 -- <files>` to see the base, and combine
  hunks.
- After any merge, verify the resolutions with
  `git merge-tree --write-tree --name-only HEAD^1 HEAD^2`, which lists the files that
  conflicted.
- **Look for silent duplicates:** when both sides added the same code in different
  places, git auto-merges both copies. This happened with Vulkan `ray_query_features`;
  linking it twice into the `pNext` chain creates a loop.
- **Shader container format:** if `ReflectionData` / `ReflectionBindingData` in
  `servers/rendering/rendering_shader_container.h` changes on either side, bump
  `CONTAINER_VERSION` above both sides' values (currently 5). A stale shader cache with
  the same version but a different layout crashes D3D12 startup (0xC0000005, garbage
  in `ReflectionBindingData` allocation).
- **Fork-specific code to preserve:**
  - `RenderForwardClusteredPT` is the Forward+ renderer, including the fallback in
    `renderer_compositor_rd.cpp`.
  - `SCENE_DATA_FLAGS_USE_DEPTH_FOG` goes after upstream's flags (currently bit 9) in
    both `scene_data_inc.glsl` and `render_scene_data_rd.h`.
  - `ShaderPreprocessor::add_define()` custom defines are applied in `_prepare_state()`.
  - Streamline/Aftermath setup, markers and cleanup in `main/main.cpp`, plus the
    `--gpu-markers`, `--debug-shaders` and `--raytracing-validation` flags.
  - New projects default to the Vulkan driver (`EditorNode::get_initial_settings()`),
    because path tracing needs Vulkan.
  - CI `static_checks.yml` runs `prek` via pip instead of the third-party action.

## Build (Windows)

`scons` is not on PATH; use `python -m SCons`.

PhysX SDK: built once with `modules/godot_physx/misc/build_physx.py --gpu --blast --flow`, pinned to
`ovphysx-0.5.11`. It's installed at
`C:\REPO\Godot\physx-sdk\physx\install\vc17win64-godot-gpu\PhysX`. Blast and Flow
come from the same checkout (`blast/` and `flow/`). Rebuild them only when the
module's `build_physx.py` pin, patches or presets change.

```
python -m SCons -k platform=windows target=editor physx_sdk="C:\REPO\Godot\physx-sdk\physx\install\vc17win64-godot-gpu\PhysX" physx_gpu=yes blast_sdk="C:\REPO\Godot\physx-sdk\blast\_build\windows-x86_64\release\blast-sdk" flow_sdk="C:\REPO\Godot\physx-sdk\flow"
```

- Output: `bin\godot.windows.editor.x86_64.exe` and `.console.exe`. `PhysXGpu_64.dll`,
  the four `NvBlast*.dll` and `nvflow.dll`/`nvflowext.dll` are copied next to it
  automatically.
- Run SCons from PowerShell or cmd. From Git Bash it can't find the D3D12 and
  AccessKit dependencies and stops at configure.
- Close any running editor from `bin\` first, or the link fails with `Access is denied`.
- Not built yet: .NET/C#
  (`module_mono_enabled=yes` + `modules/mono/build_scripts/build_assemblies.py`).

## Versioning

This build has its own version, separate from Godot's. It lives in
`CUSTOM_VERSION` at the repository root (`MAJOR.MINOR.PATCH`). Godot's own
`version.py` is never edited, so upstream merges stay clean.

- **MAJOR:** a change that breaks existing projects or removes a feature of
  this build.
- **MINOR:** a new feature of this build (for example, the in-editor DLSS
  installer), or moving to a new Godot minor version (4.8 to 4.9).
- **PATCH:** fixes, and syncs with upstream, NVIDIA or PhysX that add no
  feature of this build.

Bump `CUSTOM_VERSION` in the commit that prepares a release, not before. A
version number is never reused.

Releases on GitHub name the Godot base and the own version:

| What | Format | Example |
| --- | --- | --- |
| Git tag | `godot<major>.<minor>-<status>-nvidia-rt-dlss-physx-v<own>` | `godot4.8-dev-nvidia-rt-dlss-physx-v0.2.0` |
| Release title | `Godot NVIDIA + PhysX (Godot <major>.<minor>-<status>) v<own>` | `Godot NVIDIA + PhysX (Godot 4.8-dev) v0.2.0` |
| Zip | `godot_<major>.<minor>-nvidia-rt-dlss-physx_v<own>-editor_windows_amd64[_mono].zip` | `godot_4.8-nvidia-rt-dlss-physx_v0.2.0-editor_windows_amd64.zip` (.NET: `..._amd64_mono.zip`) |

`package_editor_win64.ps1` builds the zip name from `CUSTOM_VERSION` and
`version.py`, and prints the tag and title to use.

| Own version | Godot base | Tag | Notes |
| --- | --- | --- | --- |
| 0.1.0 | 4.8-dev | `godot4.8-dev-nvidia-rt-dlss-physx-v0.1.0` | First release (originally tagged `v4.8-dev-2026.10.08`) |
| 0.2.0 | 4.8-dev | `godot4.8-dev-nvidia-rt-dlss-physx-v0.2.0` | In-editor DLSS installer |

## Release packages (Windows)

Both editors use `production=yes`. With MSVC that means the static CRT and no
debug symbols; LTO stays off on purpose (the platform script says it doesn't
help with MSVC), so an incremental build is quick.

```
# Standard editor
python -m SCons platform=windows target=editor production=yes physx_sdk="C:\REPO\Godot\physx-sdk\physx\install\vc17win64-godot-gpu\PhysX" physx_gpu=yes blast_sdk="C:\REPO\Godot\physx-sdk\blast\_build\windows-x86_64\release\blast-sdk" flow_sdk="C:\REPO\Godot\physx-sdk\flow"
powershell -File misc/scripts/package_editor_win64.ps1

# .NET (mono) editor; needs the .NET SDK
python -m SCons platform=windows target=editor production=yes module_mono_enabled=yes physx_sdk="C:\REPO\Godot\physx-sdk\physx\install\vc17win64-godot-gpu\PhysX" physx_gpu=yes blast_sdk="C:\REPO\Godot\physx-sdk\blast\_build\windows-x86_64\release\blast-sdk" flow_sdk="C:\REPO\Godot\physx-sdk\flow"
bin\godot.windows.editor.x86_64.mono.console.exe --headless --generate-mono-glue modules\mono\glue
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
powershell -File misc/scripts/package_editor_win64.ps1 -Mono
```

The zips land in `dist/` (gitignored). The script bundles the editor exes,
`PhysXGpu_64.dll`, the Blast DLLs (`NvBlast.dll`, `NvBlastGlobals.dll`,
`NvBlastExtAuthoring.dll`, `NvBlastExtShaders.dll`), the Flow DLLs
(`nvflow.dll`, `nvflowext.dll`), the D3D12 Agility SDK DLLs, the license
files and, for mono, `bin/GodotSharp/`. Blast and Flow are always included:
the script fails if any of their DLLs is missing from `bin/`. NVIDIA
Streamline DLLs are bundled only with `-WithNvidiaRuntime` (private use; never
`bin/development/`, which holds the debug variants).

## Test

Use the console exe. A pass means exit code 0, no `ERROR:` lines, and the expected log line.

| Check | Command | Expect |
| --- | --- | --- |
| PhysX GPU, D3D12 | `--verbose --path C:\REPO\Godot\godot-physx-example --quit-after 600` (and again with `--editor`) | `PhysX 5.10.0 initialized [GPU]` |
| Path tracer, Vulkan | A Forward+ 3D scene with `Environment.pathtracing_enabled = true`, run with `--rendering-driver vulkan` | Noisy path-traced image; no "Raytracing not supported" warning |

## Known limitations

- **Path tracing is Vulkan only.** The D3D12 driver has no ray tracing implementation
  (all stubs, in upstream too). Projects set to `driver.windows="d3d12"` show a warning
  and render with rasterization.
- PhysX GPU on Linux is unverified upstream in the module.
