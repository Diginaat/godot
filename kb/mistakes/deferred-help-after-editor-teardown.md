---
type: Finding
title: Deferred script-help load outlived the editor
description: Command-line import shutdown exposed a static deferred help-cache callback with no editor lifetime guard.
tags: [editor, import, threading, lifecycle]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Evidence and narrow fix

Two expanded-suite editor --import runs failed after loading the editor layout:
EditorNode::is_cmdline_mode reported null singleton, followed by 0xC0000005.
Warm isolated repeats passed, so automated retry was not added.

Source inspection identifies a concrete late-work path: native help workers
queue static load_script_doc_cache callbacks. Unlike object-bound callbacks,
these survive EditorNode teardown. The callback checked is_cmdline_mode before
checking editor lifetime; on null singleton that returns false, allowing more
script-cache work against already-destroyed editor services.

Add an EditorNode lifetime guard at load_script_doc_cache entry. Normal live
editor behavior is unchanged. This closes the identified path; no crash stack
was available to exclude all other shutdown races. Production build, six repeated
headless editor imports (exit 0, no errors) and standard PhysX/PT/DDGI smoke pass.
