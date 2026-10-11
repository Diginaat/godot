# Agent instructions

This is a custom Godot 4 engine build combining official Godot master, NVIDIA's
DLSS/path tracer fork, and a PhysX 5 module. Before updating, merging, building or
debugging it, read [CUSTOM_BUILD.md](CUSTOM_BUILD.md). It has the sources, the update
procedure, the conflict rules, the build commands and the smoke tests.

After every sync, update the "Sync state log" table in `CUSTOM_BUILD.md`.

Path tracer work (test scene, supported features, step plan, findings) is tracked in
[PATHTRACER_TESTING.md](PATHTRACER_TESTING.md). Read it before touching the path
tracer, and update its step table and findings log as you go.

Everything the project learns (research, findings, decisions, improvement
ideas, measurements) goes into the knowledge base in [kb/](kb/index.md), an
Open Knowledge Format (OKF) v0.2 bundle. Read [kb/kb-howto.md](kb/kb-howto.md),
and update the bundle and its log in the same commit as the work.

Shared lessons and working rules for agents are in [AGENT_MEMORY.md](AGENT_MEMORY.md).
Keep it current and public-safe (no personal names, paths or private projects).
