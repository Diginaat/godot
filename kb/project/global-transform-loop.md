---
type: Finding
title: Reading a global transform recursed once per dirty ancestor
description: Node3D::get_global_transform() called itself on the parent for every dirty level, returning a 48-byte copy and checking the dirty mask each time. In scene_bench's deep scenario (1,000 chains of 100 nodes, the roots moved and the leaves read every frame) that was a fifth of the frame. A loop that collects the dirty ancestors and updates them top down took deep 100k from 18.7 to 15.1 ms and deep 10k from 0.465 to 0.432 ms.
tags: [scene-runtime, node3d, transforms, cpu]
status: stable
sources:
  - id: profile
    title: devtools/profile/profile.ps1 on scene_bench.gd --scenario=deep --count=100000, debug-symbol editor build, 2026-11-27, branch dev-renderer-next
  - id: bench
    title: devtools/test/scene_bench.ps1, deep, wide, parents, sparse, heavy at 10k and 100k (3 rounds), sparse, script, native at 100k (5 rounds), builds alternated, 2026-11-27
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

Profile of deep 100k (fork, before): the dirty walk down the chains
(`_propagate_transform_changed_walk`) 42% of samples, `get_global_transform()`
55% inclusive, of which the transform multiply was 26%. Against stock
4.7.2 the fork was 1.08x (16.8 against 18.2 ms).

`get_global_transform()` on a node whose parent is dirty called
`data.parent->get_global_transform()`, so a leaf read on a 100-level
dirty chain was 100 nested calls. Each returned its global transform by
value and re-read its dirty mask.

# Change

`Node3D::_update_global_transform_chain()`: the node and its ancestors up
to the first clean one, the root or a top-level node go into a 64-entry
array. They are then updated top down, each writing
`parent->data.global_transform * data.local_transform` in place, with
the same local update, `disable_scale` and top-level rules. A chain
longer than 64 resolves its upper part first through
`get_global_transform()` on the 64th node's parent. `get_global_transform()`
calls the loop only when the node is dirty.

# Results

`scene_bench.ps1`, builds alternated, medians:

| Scenario | Before (ms) | After (ms) |
| --- | --- | --- |
| deep 10k | 0.465 | 0.432 |
| deep 100k | 18.67 | 15.09 (stock 18.2) |
| wide, parents, heavy, sparse (10k, 100k) | same within spread | |
| script, native 100k | 22.09, 2.65 | 22.51, 2.60 (within spread) |

The dirty walk is unchanged and is now most of deep: one recursion per
node and a miss on each node and its child array.

Tried after this: prefetching the dirty bits, child array and
`xform_change` lines of the child two ahead in
`_propagate_transform_changed_walk()`. Deep 100k moved by -0.1 ms
(within spread), but wide 100k got 34% and parents 100k 43% slower:
with many children per parent the hardware already streams them, and
the extra loads only cost. Dropped.

# Tests

`[SceneTree][Node3D] Global transforms of chains longer than the update
loop's buffer`: a 150-node chain with a scaled link, a `disable_scale` link
and a top-level node, read 120 levels deep first. With the overflow
call removed it fails (130 assertions), so it covers the long-chain path.
The other SceneTree and Node tests pass.
