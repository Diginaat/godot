---
type: Success
title: Locate a crash in a production build without symbols
description: Relink with a map file, resolve the backtrace offsets, disassemble the faulting instruction, then bisect with file logs.
tags: [debugging, windows, build]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-15T00:00:00+01:00 }
---

# What worked

A `production=yes` editor crashed at startup (0xC0000005) with only
`main+<offset>` frames and no debugger on the machine. Without a full
rebuild:

1. `python -m SCons ... production=yes linkflags=/MAP` relinks only and
   writes `bin\godot.windows.editor.x86_64.map` (pass `/MAP` alone: a named
   map is overwritten by the console wrapper's link).
2. Each frame offset plus `0x140000000` is a `Rva+Base` address; the last
   public symbol at or below it (skip `$LN` labels) names the function.
   `awk` on the map takes seconds; a Python regex pass over the 195 MB file
   did not finish in ten minutes.
3. `dumpbin /disasm /range:<start>,<end>` on the exe shows the faulting
   instruction and the struct offset it reads.
4. Checkpoint logs: `fopen` with an absolute path and `fflush` per line.
   `ERR_PRINT` output and relative-path files were lost in the crash.

The cause was a null pointer read after `found != group` with both null.
The first theory (heap aliasing) came from reading the disassembly without
the checkpoints; the logs settled it in one run.

# When to use

Any crash in a release-flag build that a symbol build would take 20
minutes to reproduce.
