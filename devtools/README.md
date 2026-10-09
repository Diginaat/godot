# Developer tools

Scripts for setting up a PC, building, testing and releasing this Godot
build (upstream Godot + NVIDIA DLSS / path tracer + PhysX 5). They are
Windows PowerShell scripts; run them from PowerShell or cmd in the
repository root, not from Git Bash.

The maintenance rules (syncing upstream, NVIDIA and PhysX, conflict rules,
versioning) are in [CUSTOM_BUILD.md](../CUSTOM_BUILD.md). Feature notes:
[DDGI.md](../DDGI.md), [PATHTRACER_TESTING.md](../PATHTRACER_TESTING.md).

| Folder | Script | What it does |
| --- | --- | --- |
| `setup/` | `setup_dev_windows.ps1` | Sets up a new PC in one go (below) |
| `build/` | `build.ps1` | Builds the editor, the .NET editor, the export templates or a whole release |
| `test/` | `smoke.ps1` | The smoke tests every merge and release must pass |
| `test/ddgi/` | `ddgi_tests.ps1`, `ddgi_eval.py` | DDGI measurements: flicker, light switch, weather, scrolling, GPU time, baking, comparison shots |
| `docs/` | `update_class_docs.ps1`, `check_class_docs.py` | Keeps the class reference (editor help) complete |
| `package/` | `package_editor_win64.ps1`, `package_templates_win64.ps1` | Release zips and export template `.tpz` files |

Everything a script writes goes to `devtools/out/` (not tracked), except the
build (`bin/`) and the packages (`dist/`).

## Set up a new PC

1. Install Git, Python 3.8+ and Visual Studio 2022 (or the Build Tools) with
   the "Desktop development with C++" workload. For PhysX GPU dynamics also
   the CUDA Toolkit 12.8 (`winget install Nvidia.CUDA --version 12.8`), for
   the C# editor the .NET SDK 8 or newer.
2. Clone into a short folder (MSVC can't handle paths over 260 characters,
   and the build's deepest files add about 150), then run the setup:
   ```
   git clone -b nvidia-dlss-physx https://github.com/Diginaat/godot.git godot-rtx
   cd godot-rtx
   powershell -ExecutionPolicy Bypass -File devtools\setup\setup_dev_windows.ps1 -Build
   ```

The setup checks the tools, installs SCons and Godot's D3D12 build
dependencies, sets the git remotes the runbook uses (`upstream`, `origin` =
NVIDIA-RTX, `physx`, `fork` = Diginaat), builds the PhysX, Blast and Flow
SDKs once in a `physx-sdk` folder next to the repository (about 20 minutes
the first time), writes the SDK paths into `custom.py` and, with `-Build`,
builds the editor. Run it again any time: finished steps are skipped.

Options: `-Mono` (also the .NET editor), `-NoGpu` (PhysX without CUDA),
`-PhysXDir <folder>` (build the SDKs somewhere else).

`custom.py` holds this PC's build options and is read by every SCons run, so
builds need no long command lines. It is not tracked by git.

DLSS is optional: in the editor, click **Get NVIDIA DLSS...** to install
NVIDIA's runtime files next to it.

## Build

```
devtools\build\build.ps1                         # editor
devtools\build\build.ps1 -Mono                   # .NET editor, with C# glue and assemblies
devtools\build\build.ps1 -Target templates       # release and debug export templates
devtools\build\build.ps1 -Target templates -Mono # .NET export templates
devtools\build\build.ps1 -Target release         # all six builds a GitHub release needs
```

`-KeepGoing` reports all compile errors in one pass (scons `-k`); `-Jobs N`
limits the parallel jobs. All builds use `production=yes`: staying on one
flag set keeps them incremental. Close any editor running from `bin\` first,
or linking fails with "Access is denied".

## Test

```
devtools\test\smoke.ps1          # standard editor
devtools\test\smoke.ps1 -Mono    # .NET editor
```

| Test | Passes when |
| --- | --- |
| PhysX, game and editor | `PhysX 5.x initialized` in the log, exit code 0, no errors |
| Path tracer, Vulkan | a screenshot is written, no "Raytracing not supported" |
| DDGI, interior | the test scene renders and measures, no errors |

The PhysX tests use Wild Ox Studios' example project; clone it next to this
repository (`git clone https://github.com/uno1982/godot-physx-example`) or
pass `-PhysXExample <folder>`. Without it they are skipped. Logs and the
screenshot are in `devtools\out\smoke`.

### DDGI measurements

```
devtools\test\ddgi\ddgi_tests.ps1 -Test flicker    # static interior: brightness and flicker
devtools\test\ddgi\ddgi_tests.ps1 -Test switch     # lights switched: how fast the GI follows
devtools\test\ddgi\ddgi_tests.ps1 -Test weather    # a storm rolls in: patchiness and lag
devtools\test\ddgi\ddgi_tests.ps1 -Test scroll     # moving camera: error left by scrolling
devtools\test\ddgi\ddgi_tests.ps1 -Test bench      # GPU time per DDGI pass at 1080p
devtools\test\ddgi\ddgi_tests.ps1 -Test bake       # bake and load the interior volume
devtools\test\ddgi\ddgi_tests.ps1 -Test compare    # the README comparison shots
devtools\test\ddgi\ddgi_tests.ps1 -Test all
```

Add test project arguments with `-Extra`, for example
`-Extra "--quality=3","--realtime=1"`. What each number means, and the
results so far, are in [DDGI.md](../DDGI.md). The image measurements need
Pillow and NumPy (`python -m pip install pillow numpy`).

## Class reference

```
devtools\docs\update_class_docs.ps1
```

Updates the XML of the editor help from the built editor, restores the docs
of classes the build doesn't include, lists every fork entry that still has
no description and validates the XML. `python devtools\docs\check_class_docs.py -v`
only does the listing.

## Release

1. Bump `CUSTOM_VERSION`, update the versioning table in CUSTOM_BUILD.md and
   the version line in README.md, commit ("chore(release): prepare x.y.z").
2. `devtools\build\build.ps1 -Target release` (the version stamp is the commit).
3. `devtools\test\smoke.ps1` and `devtools\test\smoke.ps1 -Mono`.
4. Package:
   ```
   powershell -File devtools\package\package_editor_win64.ps1
   powershell -File devtools\package\package_editor_win64.ps1 -Mono
   powershell -File devtools\package\package_templates_win64.ps1
   powershell -File devtools\package\package_templates_win64.ps1 -Mono
   ```
   The scripts print the release tag and title. Public packages never contain
   NVIDIA Streamline / DLSS files; games exported with the editor get them
   from the editor's folder.
5. Push the branch and the tag, then
   `gh release create <tag> --target <full commit SHA> --title "<title>" dist\...`.
