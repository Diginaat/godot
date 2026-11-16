# Hybrid scene runtime: architecture analysis (Phase 0)

Goal: lower main-thread CPU cost of Godot's Node / SceneTree / Node3D
machinery for large and dynamic scenes, beneath the existing API, with no
workflow, API or behaviour change. Roadmap:
[scene-runtime-roadmap.md](../implementation/scene-runtime-roadmap.md);
numbers: [scene-runtime.md](../benchmarks/scene-runtime.md); state:
[docs/scene-runtime/MEMORY.md](../scene-runtime/MEMORY.md).

## Baseline

Godot 4.8-beta (upstream master `b47a795aa5` plus this fork). The fork has
not changed `scene/main/node.*`, `scene_tree.*`, `scene/3d/node_3d.*` or
`scene/2d/node_2d.*` (only `viewport.*` and `window.*` in `scene/main`), so
everything below is upstream behaviour. Branch `feat/scene-runtime`, on top
of `feat/core-stl-simd-modernization` (its profiling tools and core
changes D012-D014).

## How it works today

### Node (`scene/main/node.h`, `node.cpp`)

- `Node::Data` holds the parent and owner pointers, `children` as
  `HashMap<StringName, Node *>` (by name, insertion ordered; a heap node per
  child), `children_cache` (`LocalVector<Node *>` in index order, rebuilt
  lazily), `owned_unique_nodes` and `grouped` (two more `HashMap`s), `owned`
  (a `List`), the process owner and thread group, priorities and bit flags.
- `children_cache`: appending a child or removing the last one keeps it
  valid; removing any other child marks it dirty, and the next access
  (`get_child`, `iterate_children`, notifications to children) rebuilds it:
  copy every child from the name map, sort by index, rewrite each child's
  `data.index`. That touches every child of the parent.
- Notifications (`_propagate_enter_tree`, `_propagate_ready`,
  `_propagate_exit_tree`, ...) recurse over the cache.

### SceneTree (`scene/main/scene_tree.*`)

- Process lists: per process group a `Vector<Node *>` for idle and one for
  physics processing, sorted by priority only when dirty; each frame copies
  the vector (copy-on-write: no allocation) and calls the notifications of
  each node that can process. Thread groups run on `WorkerThreadPool` with
  their own call queues. The per-node cost is the notification dispatch and
  the script call, not the list.
- Groups: `group_map` (`HashMap<StringName, SceneTreeGroup>`), each group a
  `Vector<Node *>` re-sorted into tree order lazily (`changed`).
  `add_to_group` checks `nodes.has(p_node)` (linear) before appending;
  `remove_from_group` erases (find and shift, linear). It is called for
  every group of every node entering or leaving the tree.
- `xform_change_list`: Node3D and CanvasItem nodes that want
  `NOTIFICATION_TRANSFORM_CHANGED` are queued when their transform changes
  and notified once per frame by `flush_transform_notifications()`.

### Node3D (`scene/3d/node_3d.*`)

- Local and global transforms cached in the node; dirty bits are atomic
  (`DIRTY_LOCAL_TRANSFORM`, `DIRTY_EULER_ROTATION_AND_SCALE`,
  `DIRTY_GLOBAL_TRANSFORM`, `DIRTY_GLOBAL_INTERPOLATED_TRANSFORM`).
- Writes are eager: `set_transform()` and the other setters call
  `_propagate_transform_changed()`, which recurses over
  `data.node3d_children` (a `LocalVector` of the Node3D children, upstream
  PR #107481), skips `top_level` children, queues nodes with
  `notify_transform` (and gizmos in the editor) on `xform_change_list`, and
  sets the global dirty bits. There is no early stop at descendants that are
  already dirty.
- Reads are lazy: `get_global_transform()` recomputes only when dirty,
  recursing up through dirty parents, and caches the result. So a clean
  node has clean ancestors, and a dirty node has dirty descendants.
- Physics interpolation (FTI) keeps previous transforms and its own lists
  in `SceneTree`; `get_global_transform_interpolated()` reads them.

Node2D / CanvasItem follow the same pattern (`_notify_transform`, lazy
`get_global_transform()`, their own child vectors).

## Where the time goes

Sampling profiles (`devtools/profile/`, editor build with symbols,
2026-11-15/16; details in [kb/project/core-cpu-profiles.md](../../kb/project/core-cpu-profiles.md)),
share of the busiest thread's busy samples:

| Workload | Scene-side costs found |
| --- | --- |
| 10k moving `MeshInstance3D`, 200 freed and added per frame under one parent | `Node::_update_children_cache_impl` 5.8% (rebuilds after non-last removals), `Span::find` in `SceneTree::add_to_group` 2.0%, `DynamicBVH` (renderer indexers) 9% (D014 cut it), Node3D propagation and `xform_change_list` |
| 2k-node scene instanced and freed per frame | heap calls 14.6% (signal `connect` 2.5%, renderer dependency tracking, `ClassDB::creator`), hash lookups of signals, StringName construction 3.4% |
| Editor startup | Theme lookups, StringName, CowData |

The renderer side of the same workloads (RID checks and locks,
dependency-tracking sets, BVH kernels) was addressed in D012-D014. What is
left on the scene side is algorithmic: work proportional to a parent's or a
group's size on each mutation, and allocations per child, connection and
group membership. The scenario benchmark
([numbers](../benchmarks/scene-runtime.md)) measures these one by one.

## Approaches compared

### A. Targeted fixes inside the existing structures

Keep `Node`, `SceneTree` and `Node3D` as they are and remove the measured
costs where they arise: keep the children cache valid on removals (or make
its rebuild proportional to what changed), make group membership changes
constant time, stop transform propagation at subtrees that cannot need
more work, cut allocations per child / group / connection.

- Benefit: every project, no new state, small reviewable diffs; each change
  can be measured and reverted on its own.
- Cost: no change of data layout, so traversal of large trees keeps chasing
  pointers.
- Risk: low; observable behaviour (orders, notifications, indices) must be
  kept exactly, which each change tests.

### B. Compact hierarchy and transform store (data-oriented backend)

A per-tree index of Node3D nodes in arrays (parent index, depth, local and
global transform, dirty version) maintained beside the nodes, so that
propagation and recomputation run over contiguous memory in depth order
(breadth-first like Flecs cascades, or per root like Unity's
`LocalToWorldSystem`), with SIMD and parallel subtrees.

- Benefit: large for "every node moves every frame" workloads where the
  matrix products dominate.
- Cost: two places holding transforms (or moving Node3D's authoritative
  transform into the store, touching every subclass that reads
  `data.local_transform`), index maintenance on every add/remove/reparent,
  `top_level`, `disable_scale`, physics interpolation and the editor's
  gizmo transforms in the new path.
- Risk: high. Godot's lazy reads already skip nodes nobody asks about; the
  measured cost of moving nodes is in notifications and server updates, not
  in matrix products. Worth it only if a benchmark shows the products or the
  traversal dominate after A.

### C. ECS-backed runtime beneath the nodes

Nodes become views on archetype storage; transform, visibility and server
sync run as scheduled systems (Bevy-style change detection, `mark_dirty_trees`
skipping static trees).

- Benefit: the most for very large, uniform scenes.
- Cost: a second object model under Godot's; every node type, script
  binding, notification and editor tool must keep its exact semantics on top
  of it; immediate getters force synchronous fallbacks that erase much of the
  gain.
- Risk: very high; contradicts "no duplicate authoritative state" and
  "improve the existing architecture first".

### Decision

Start with A, guided by the profiles and the scenario benchmark, one
measured change at a time. Take ideas from B and C only where a benchmark
shows the cost they remove: Bevy's dirty-tree skipping maps onto an early
stop in `_propagate_transform_changed()` (Phase 2); breadth-first batched
recomputation (B) is a later experiment for the deep / heavy-update
scenarios, behind a measured threshold, if their profiles show matrix work
rather than notifications. No duplicate authoritative state at any stage.

## Final state (M9, 2026-11-16)

Approach A held: every kept change is inside the existing structures, with
no second copy of hierarchy or transform state and no API change.

| Change | Where | Effect |
| --- | --- | --- |
| M1: no duplicate scan when a node joins a group (dev builds still check) | `SceneTree::add_to_group()` | group joins O(1) |
| M2: removed children leave holes, compacted on the next read | `Node::data.children_cache` | no rebuild and sort per removal |
| M3: a member's slot recorded, removal leaves a hole | `SceneTreeGroup`, `Node::GroupData::position` | group leaves O(1) |
| M4: propagation stops at dirty subtrees walked in the current notification epoch | `Node3D::_propagate_transform_changed()`, `SceneTree::xform_change_epoch` | repeated moves of a subtree walk it once per flush |
| M5: a node's groups in an ordered vector; no `StringName` built per instanced node | `Node::data.grouped`, `SceneState::_instantiate()` | one allocation instead of three; nodes 24 bytes smaller |
| M6: the process loop prefetches the next nodes | `SceneTree::_process_group()` | overlapping cache misses in large process lists |

Not done, with reasons in the roadmap: a contiguous transform store (B)
and batched recomputation (M7, skipped), 2D counterparts (M8: 2D already
has the early stop), pooled child and owner bookkeeping (M5 rest: about
2%), an ECS beneath the nodes (C).

Totals against the base, release, headless: scene instancing 2.2x (10k)
and 10.7x (100k), node churn in groups 2.8x and 11.4x, every node moving
-11% and -41%, 100k processing nodes -44%, GDScript processing -3 to -5%;
no scenario slower beyond the code layout spread
([benchmarks](../benchmarks/scene-runtime.md#m9-original-against-final-2026-11-16)).

## Compatibility constraints (all phases)

- Child order, `get_index()`, internal front/back children, `move_child`.
- Group order as `get_nodes_in_group()` and `call_group()` return it (tree
  order after `_update_group_order`), group membership errors.
- Notification order and counts (`ENTER_TREE`, `READY`, `EXIT_TREE`,
  `TRANSFORM_CHANGED` once per change per frame, `LOCAL_TRANSFORM_CHANGED`
  immediately).
- `get_global_transform()` always current, also from threads that may read
  it; `top_level`, `disable_scale`, reparenting, FTI.
- Thread groups (`process_thread_group`), deferred calls, removal during
  processing (`nodes_removed_on_group_call`).
- GDScript, C# and GDExtension see the same API; the editor and tool scripts
  the same behaviour.

## Profiling and measuring

- Scenario benchmark: `devtools/test/scene_bench.ps1` (12 scenarios, 1k/10k/100k
  nodes, headless or `-Visual`, builds alternated per round).
- Workload profiles: `devtools/profile/` (sampler with inlined frames).
- Release numbers from a `template_release` build with symbols; editor
  builds carry `DEBUG_ENABLED` allocation accounting.
- A control build of the same source with the change off when a result is a
  few percent (code layout alone moves a build by about 1.5%;
  [success page](../../kb/successes/control-build-for-layout-effects.md)).

## References

- Bevy `bevy_transform` systems (`mark_dirty_trees`, static scene
  optimization off above 30% moving entities):
  https://docs.rs/crate/bevy_transform/latest/source/src/systems.rs
- Unity Entities transforms (`LocalToWorldSystem` after `ParentSystem`,
  recursive per hierarchy): https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/transforms-concepts.html
- Flecs cascade queries (breadth-first by depth):
  https://github.com/flecs-hub/flecs-systems-transform/blob/master/src/main.c
- Godot PR #107481 (`node3d_children` and CanvasItem child vectors).
