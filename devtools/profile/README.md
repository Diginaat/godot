# CPU sampling profiles without elevation

Kernel tracing (`xperf`, `wpr`) needs an elevated shell. `sampler.exe`
needs none: it opens a process of the same user, suspends each thread that
used CPU since the last tick, walks its stack, resumes it, and at the end
resolves the addresses with the PDB, inlined frames included. The innermost
inline frame is what matters for header-only code such as `HashMap`, which
is mostly inlined into its callers.

1. Build the editor with symbols: `production=yes debug_symbols=yes`
   (same optimization, plus a PDB next to the exe).
2. Profile a workload (the sampler is built on first use):

   ```powershell
   # --script is resolved inside the project unless it is absolute.
   devtools\profile\profile.ps1 -Label instance -GodotArgs @("--path", "misc\large_world_test_project", "--script", "$PWD\devtools\profile\core_workload.gd", "--", "--mode=instance", "--seconds=24")
   devtools\profile\profile.ps1 -Label editor -Warmup 1 -Seconds 25 -GodotArgs @("--editor", "--path", "misc\large_world_test_project", "--quit-after", "3000")
   ```

   Opening the test project in the editor rewrites its `project.godot`
   (it drops the explicit driver and renderer) and some `.import` files;
   restore them with `git checkout --` on those files before
   committing or running the fixtures.

3. Read it:

   ```powershell
   python devtools\profile\analyze.py devtools\out\profile\instance.tsv summary
   python devtools\profile\analyze.py devtools\out\profile\instance.tsv callers "^(HashMap|HashSet|AHashMap)::"
   python devtools\profile\analyze.py devtools\out\profile\instance.tsv callers "AcquireSRW|ReleaseSRW|mtx_do_lock" --skip core/os
   ```

`core_workload.gd` (a `SceneTree` script, vsync off) has three modes:
`nodes` (10k moving nodes, 200 freed and added per frame, groups),
`script` (GDScript logic with dictionaries, arrays, strings, vectors) and
`instance` (a 2k-node packed scene instanced and freed every frame).

Limits: about 0.5-1 kHz per thread; stack walks slow the sampling when a
process has many threads (the editor: about 100 Hz). Waits are recognized by
the system function's name (`analyze.py`), not by the scheduler. Editor
binaries have `DEBUG_ENABLED` allocation accounting that release templates
do not. Findings: [kb/project/core-cpu-profiles.md](../../kb/project/core-cpu-profiles.md).
