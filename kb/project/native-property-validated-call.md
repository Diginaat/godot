---
type: Finding
title: Native property access paid for MethodBind::call()'s argument checks
description: Every get or set of an engine property by name (GDScript's member access to native properties, Object.get/set) called the getter or setter through MethodBind::call(), which checks and converts each argument. A getter's arguments are always known, and a setter's value usually has the declared type. Using validated_call() in those cases made scene_bench's scripted processing 5-6% faster, with the same behavior.
tags: [gdscript, object, properties, methodbind, scene-runtime]
status: stable
sources:
  - id: bench
    title: devtools/test/scene_bench.ps1 -Scenarios script,native,sparse,mixed -Counts 10000,100000, 3 rounds, alternated builds, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

GDScript compiles a native property of the script's base (`position`
in a script that extends Node3D) to `OPCODE_GET_MEMBER` and
`OPCODE_SET_MEMBER`, which call `Object::get_native()` and
`set_native()`. Those find the property in the type's member table,
then call the getter or setter `MethodBind` with `call()`. `call()`
checks the argument count, fills defaults and converts every argument
from Variant. The scene bench script (`position.y = sin(t)`) does a get
and a set per node per frame.

# Change

In `core/object/object.cpp`:

- Getters use `validated_call()`, with the return Variant initialized to
  the getter's return type. Their arguments are always known: none, or
  the property's int index.
- Setters use it when the value's type is the declared one, or the
  argument is a Variant.
- Kept on `call()`:
  - Objects, arrays and dictionaries, where `call()` checks the class or
    the element types.
  - Values that need a conversion (int to float, String to StringName).
  - Vararg binds, binds with default arguments, and setters that return
    a value.

The compile-time alternative, emitting validated getter and setter calls
in the GDScript compiler, would bind the property of the script's native
base. A script can be attached to a subclass that registers the same
property again, so that needs a run-time guard. This change keeps the
per-object member lookup.

# Results

scene_bench, median of 3 alternated rounds (frame ms):

| Scenario | Before | After |
| --- | --- | --- |
| script 10k | 1.132 | 1.075 |
| script 100k | 23.59 | 22.13 |
| native 10k / 100k | 0.147 / 2.53 | 0.147 / 2.50 |
| sparse, mixed 100k | 0.451 / 0.413 | 0.437 / 0.437 (spread) |

The check script covered set and get of Vector3, bool, int, enum,
String, packed arrays, Transform3D, indexed float properties, objects
and null. It also tried converted values (Vector3i to Vector3, int to
float, String to StringName), a wrong class and a wrong type, and
printed the same results on both builds.
