---
type: Mistake
title: A per-frame readback callback outlived the renderer
description: The GPU path's render-info readback, requested every frame, completed during shutdown and crashed in a destroyed HashMap; the crash looked like the known flaky quit crash.
tags: [mistake, renderer, lifecycle, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-10T00:00:00Z }
---

# What happened

The GPU path's viewport render info reads a 16-byte counter back with
`buffer_get_data_async` every frame. Its static callback checked only
`RenderForwardClustered::get_singleton()`, which is never cleared. The last
readbacks complete while the RenderingDevice shuts down, after the renderer
is destroyed, and `HashMap::getptr` on its freed member crashed
(0xC0000005) in about 70% of `gpu_instances.ps1` runs.

It passed the first runs after the commit by chance. Later failures looked
like the known flaky quit crash, and an older build crashed too, which
suggested the environment. A bisect of the newer change found nothing.

What found it: the Windows Application log (event 1000) gives the same fault
offset each time; relinking with `linkflags=/MAP` and looking the offset up
in `bin/godot.windows.editor.x86_64.map` named the function in a minute,
without a debug build.

# Rule

- A static callback of an async readback must check a lifetime flag that the
  owner clears in its destructor (`gpu_instances_readbacks_alive`), not a
  singleton pointer that stays set.
- A crash on quit is a regression until shown otherwise: check whether it
  has one fault offset (event log), and resolve it with a map file before
  calling it flaky. Run a changed test several times before committing.
