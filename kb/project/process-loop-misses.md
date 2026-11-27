---
type: Finding
title: Scripted node processing: an allocation per notification and unprefetched lines
description: Sampling profiles of 100k nodes with a GDScript _process() found 11% of the main thread in an allocation that GDScriptInstance::notification() made for every notification, and most of the rest in cache misses on lines the process loop's prefetch did not cover (the script instance pointer, the script instance, Node3D's local transform). Skipping the allocation when no script in the chain has _notification(), and prefetching those lines, made the script scenario 23% (10k) and 35% (100k) faster with native processing unchanged.
tags: [scene-runtime, gdscript, process, prefetch, cpu, memory]
status: stable
sources:
  - id: bench
    title: devtools/test/scene_bench.gd (script, native), headless, editor build production=yes debug_symbols=yes, 2026-11-27, branch dev-renderer-next, 3 rounds alternating builds
  - id: sampler
    resource: ../../devtools/profile/README.md
    title: devtools/profile/sampler.exe, 15 s per workload
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

The stock comparison ([stock-compare-2026-11](/project/stock-compare-2026-11.md))
had the fork only 1.2x faster than stock 4.7.2 for nodes with a GDScript
`_process()` (`scene_bench.gd` script: `t += delta; position.y = sin(t)`),
and slower for 10k running timers (native: 0.146 against 0.125 ms).

Profile of script at 100k nodes, main thread:

- `GDScriptInstance::notification()` built a `LocalVector` of the script's
  inheritance chain for every notification, then looked for
  `_notification()` in each script. Every process frame of every scripted
  node paid a heap allocation and free: 11% (`LocalVector::push_back`
  5.6%, `RtlSizeHeap`, `RtlFreeHeap`).
- `Node::_gdvirtual__process_call` 25.6% self: the first touch of the
  script instance. `Object::script_instance` lies far from the object's
  start (after the signal maps), outside the lines the process loop
  prefetched (M6 in the scene runtime roadmap).
- `Node3D::get_position` 14.6%: the local transform is about 140 bytes
  into Node3D's data.

In native, `Object::_notification_forward()` spent 11% on reading
`script_instance` (also a miss), and `HashSet::has()` on the empty
removed-node set cost 5%, because its lookup function is not inlined.

# Changes

- `GDScriptInstance::notification()` first checks whether any script in
  the chain has `_notification()`; most have none and return at once.
- `SceneTree::_process_group()`:
  - also prefetches the script instance pointer's line and the first line
    after `Node` (where a subclass's fields start, such as a Timer's
    state), 8 nodes ahead;
  - 4 nodes ahead, for a node still in the tree, reads the script instance
    pointer (that line has arrived) and prefetches the script instance, the
    instance ID line and, for a Node3D, the local transform;
  - checks `is_empty()` before `has()` on the removed-node set.
- Small accessors that only return addresses:
  `Object::_get_script_instance_address()`, `_get_instance_id_address()`,
  `Node3D::_get_local_transform_address()`.

# Results

Frame ms, medians of 3 rounds, same machine and build options:

| Scenario | Before | Allocation fix | And prefetch | Stock 4.7.2 |
| --- | --- | --- | --- | --- |
| script 10k | 1.486 | 1.178 | 1.141 | 2.33 |
| script 100k | 36.1 | 33.1 | 23.6 | 44.4-45.6 |
| native 10k | 0.148 | | 0.145 | 0.123-0.127 |
| native 100k | 2.71 | | 2.53 | 4.16-4.20 |

# Failed variants

- Prefetching two more lines after `Node`, blindly (+64, +128 bytes):
  script 100k −11%, but native 100k +21% (lines past a small Timer's end).
- Prefetching the instance ID line for every node: native 100k +20-25%.
  With about seven prefetches per node, eight nodes ahead, there are more
  misses in flight than the core keeps. Extra lines now go only to
  scripted nodes, at the second stage.

# Open

- Native 10k is still about 0.02 ms (15%) slower than the official 4.7.2
  build. What is left is the virtual notification chain and misses on the
  Timer state. Part of it may be the toolchain: the official Windows
  builds use LTO with another compiler, this build uses MSVC without LTO.
  Not checked.
- Native property access from GDScript (`position`) goes through
  `Object::get_native()` / `set_native()` with Variant arguments: about
  13% of script 100k beyond the getter and setter themselves. A validated
  call needs a guard for subclasses that rebind a property.
