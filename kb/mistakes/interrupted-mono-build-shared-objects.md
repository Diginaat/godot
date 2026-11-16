---
type: Mistake
title: An interrupted .NET build left its objects for the standard build
description: Standard and .NET (mono) builds share object files and generated sources; stopping SCons during the .NET editor build left .NET-configured files that the next standard build took as up to date, and the standard editor failed to link against mono symbols.
tags: [mistake, builds, mono, scons]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-17T00:15:00+01:00 }
---

# What happened

For the 0.10.1 release the six builds ran in one script: editor and both
templates, then the same with `module_mono_enabled=yes`. The run was stopped
during the .NET editor build. The next run's standard editor failed to link:

`unresolved external symbol "void __cdecl initialize_mono_module(...)" referenced in function initialize_modules`

The standard and .NET builds write the same object names (`*.windows.editor.x86_64.obj`)
and the same generated sources (`modules/modules_enabled.gen.h`,
`modules/register_module_types.gen.cpp`, version and icon headers). SCons
saves its signature database at the end of a run, so the stopped run's
rewrites were never recorded: the next standard build compared its sources
with the records of the last standard build, found them unchanged and kept
the .NET-configured files on disk.

# Rule

Do not stop a build in the middle of the .NET part. If one was stopped,
delete the ignored build outputs written since that step started (objects,
libraries and `.gen.*` sources:
`git ls-files -o -i --exclude-standard` filtered by modification time),
then build again; 122 files here, about the cost of an incremental build.
