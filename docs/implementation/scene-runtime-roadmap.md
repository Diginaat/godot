# Scene runtime roadmap

Approach A of the [architecture analysis](../architecture/hybrid-scene-runtime.md):
measured fixes inside Node / SceneTree / Node3D, one per milestone, each
buildable, testable and revertible on its own. State and results:
[docs/scene-runtime/MEMORY.md](../scene-runtime/MEMORY.md),
[benchmarks](../benchmarks/scene-runtime.md).

Rules for every milestone:

- Baseline and candidate in the same session, builds alternated
  (`devtools/test/scene_bench.ps1 -Builds base,candidate`), at least 3
  rounds; a control build of the same source when the effect is a few
  percent.
- Keep only a reproducible gain with no regression elsewhere in the 12
  scenarios beyond the spread; otherwise revert and record it.
- Unit tests for the behaviour the change touches (orders, indices,
  notifications), the full suite, the standard smoke.
- Before building: read the code, profile the target workload, write down
  the per-node or per-call cost each candidate adds to hot paths, pick one
  design. Then build it once (editor with tests, release template) and
  compare it with one control build (the unchanged source with a code
  layout change, e.g. an unused function).
- Judge on the scenario's `op` time and on headless runs (`--headless` with
  `--visual` for MeshInstance3D notifications on a dummy renderer). Windowed
  `-Visual` frame time is renderer-bound and moves 2-11% with code layout
  alone; use it only for larger effects.
- Commit, update this table and the memory file, push the branch.

| # | Milestone | Measures | Tests | Rollback if | State |
| --- | --- | --- | --- | --- | --- |
| M0 | Phase 0: analysis, scenario benchmark, baseline | all 12 scenarios, 1k/10k/100k, editor and release | benchmark runs without errors | - | done 2026-11-16 |
| M1 | `SceneTree::add_to_group`: the linear duplicate check only in dev builds (both callers already guarantee a new member) | `churn`, `instance`, enter/exit with groups | group membership and order through add/remove/re-enter | any scenario slower | done: `instance` 100k -60%, `churn` 100k -51% |
| M2 | Children cache: a removal leaves a hole, the next reader compacts it in place (no walk of the name map, no sort); updating it on every removal would cost more when many removals come before one read | `churn`, `reparent`, wide parents | child order, `get_index()`, internal front/back children, `move_child` after removals (test in place) | `wide`/`churn` not faster, or removals of front children much slower | done: `churn_read` -15-17%, profiled node workload -6.4% |
| M3 | `remove_from_group` without a linear search (member index or tree-order rules) | `churn`, `instance` | `get_nodes_in_group` order, `call_group` during removal | group order changes | done: `churn` 100k -81%, `instance` 100k -70%, 10k -17-39% |
| M4 | Node3D transform propagation stops at subtrees that need nothing more (per-subtree count of nodes that want `TRANSFORM_CHANGED`, plus the dirty invariant) | `deep`, `parents`, `heavy`, `-Visual` | notification counts per frame, global transforms after random moves, top_level, reparent, FTI | any notification lost or delayed | done: `heavy` op 10k -12%, 100k -32%; `wide`, `parents` op within 1.3% |
| M5 | Allocations per child, group membership and signal connection (pooled or inline storage where the profile shows them) | `instance`, `churn`, allocation counts | lifecycle, ownership, signals | memory up without time down | done (groups part): `instance` -20-24%, `churn` -8-10%; node 24 bytes smaller |
| M6 | Process lists: incremental priority order, cheaper per-node checks (only if a script-free profile shows them) | `native`, `script` | process order with priorities and pause modes | callback order changes | done: `native` 100k -40%, `script` 100k -5% |
| M7 | Experiment: breadth-first batched Node3D recomputation for the deep / heavy cases (approach B), behind a measured threshold | `deep`, `heavy`, `parents` at 100k | as M4 | no gain over M4 | |
| M8 | Node2D / CanvasItem counterparts of M2-M4 | `mixed`, 2D variants | as M2-M4 | as M2-M4 | |
| M9 | Integration: full benchmark set, a game level, editor, C#, GDExtension | everything | full suite, smoke, large-world, editor | - | |

Order can change when a profile shows a larger cost elsewhere.

## M4 notes (from the Phase 0 audit)

- Invariant today: `get_global_transform()` clears a node's
  `DIRTY_GLOBAL_TRANSFORM` only after its parent's (it recurses up), and
  propagation marks whole subtrees (skipping `top_level`). So a dirty node's
  non-top-level Node3D descendants are all dirty, and propagation could stop
  at a child that is already dirty.
- What blocks it: propagation also queues nodes that want
  `NOTIFICATION_TRANSFORM_CHANGED` (`notify_transform`, editor gizmos). A
  dirty node is not always queued: `flush_transform_notifications()` dequeues
  it, and it stays dirty if its handler does not read its global transform
  (engine nodes like `VisualInstance3D` do read it; a script with
  `set_notify_transform(true)` may not); `set_notify_transform(true)` on a
  dirty node does not queue it.
- A design to evaluate: make "dirty and wants notifications" imply "queued"
  (the flush cleans the global transform of each notified Node3D, and
  enabling notifications on a dirty node queues it), then stop propagation at
  dirty children. Also audit every `_set_dirty_bits(DIRTY_GLOBAL_TRANSFORM)`
  / `_replace_dirty_mask()` site (setters clear and re-set the node's own bit
  around the propagation), deferred queueing from threads, physics
  interpolation's own lists.
- Expected effect: `heavy` and `sparse` in balanced trees (each move re-walks
  the subtree: work proportional to depth times nodes) become linear.

## M4 as implemented

- `Node3D::_propagate_transform_changed()` reads `SceneTree::xform_change_epoch`
  once and walks the subtree (`_propagate_transform_changed_walk()`), which
  writes the epoch into each node it walks (`Node3D::Data::propagated_epoch`,
  in the padding next to the dirty bits; `sizeof(Node3D)` unchanged, 1104
  bytes). It skips a non-top-level child whose `DIRTY_GLOBAL_TRANSFORM` and
  `DIRTY_GLOBAL_INTERPOLATED_TRANSFORM` are set and whose epoch is current.
- Why that is safe: a dirty node's subtree is all dirty
  (`get_global_transform()` cleans a node only after its parent), and the
  walk that wrote the epoch queued every node in it that wanted
  `NOTIFICATION_TRANSFORM_CHANGED`. The epoch is bumped whenever that can
  stop being true: after a flush that notified anything (at the end, since
  handlers can move nodes during the flush), after
  `force_update_transform()`, and when a dirty node starts to want
  notifications (`set_notify_transform(true)`,
  `set_ignore_transform_notification(false)`, `add_gizmo()`). Nodes that
  enter the tree are queued by `NOTIFICATION_ENTER_TREE`; nodes that leave
  leave the subtree.
- Nothing is added per notification: the bump is one atomic increment per
  flush. Without notifications (plain Node3D trees) the epoch does not
  change, and repeated moves stop at once.
- Physics interpolation: its optimized traversal clears
  `DIRTY_GLOBAL_INTERPOLATED_TRANSFORM` below parents that keep it, so the
  early-out is off while interpolation is on, and
  `SceneTreeFTI::_reset_flags()` (it walks every node when interpolation is
  toggled) sets the bit everywhere. After 2^32 bumps the early-out turns
  off for good (no wrap-around equality).
- Threads: the epoch is a relaxed atomic (thread-group nodes bump it); a
  node queued through a deferred call is dirty and not yet queued while the
  call is pending, and the call queues it, as before.
- Test: `tests/scene/test_node_3d.cpp` runs 4,000 random moves, rotations,
  reads, notification and ignore toggles, top-level toggles, reparenting and
  flushes on an 80-node tree against a model of the old propagation (every
  node of a moved subtree that wants notifications is queued, each queued
  node notified once per flush), with and without physics interpolation
  toggled, and checks every global transform against a product of local
  transforms. With an earlier design, removing any one of its guards made
  it fail.
- Rejected on the way (measured): cleaning each notified node in its
  `NOTIFICATION_TRANSFORM_CHANGED` handler or after the handlers in the
  flush cost 50-75 ns per notified node (`wide` with MeshInstance3D,
  headless).

## M5 as implemented

Profile first (release, `instance` 10k and `churn` 100k, headless): heap
calls were 26% of the `instance` main thread. Per instanced node: the
node's `grouped` HashMap (three allocations for its first group, three
frees: 6.3% plus part of `~Node`'s 4.2%), the object itself
(`ClassDB::creator`, 4.9%), the parent's `children` map element (1.5%),
the owner's `owned` list element (1.3%), and a `StringName` built from a
literal per node in `SceneState::_instantiate()` (`remove_meta()` of the
pinned-properties key, about 1.2% with its hash, table lookup and unref).

- `Node::Data::grouped` is a `LocalVector<GroupEntry>` (name, data) in
  insertion order, searched by `StringName` pointer (`Node::_find_group()`):
  one allocation for the first group, `get_groups()` order unchanged,
  removal keeps the others' order. A node is in few groups; a node in very
  many pays a linear search per lookup.
- `SceneState::_instantiate()` uses `SNAME()` for that key.
- `sizeof(Node)` 736 to 712 bytes, `Node3D` 1104 to 1080.
- Not done (smaller in the profile): `children` and `owned` element
  allocations, signal connections.

## M6 as implemented

Profile first (release, `native`: 100k Timers, internal processing, no
scripts): 93% of the main thread in `SceneTree::_process_group()`, almost
all of it the first read of each node's cache lines (`Node::can_process()`
64.7%, `_can_process()` 13.9%, `Timer::_notification()` 9.9%): about
115 ns per node, DRAM misses on the six lines the checks and the dispatch
read (object start, `script_instance`, `data.tree`, `process_owner`, the
process mode and flags, the Timer's fields; offsets from the PDB). The
checks' logic costs nothing; sorting and list maintenance do not show.

- `_process_group()` prefetches the object start and the three `Node::Data`
  lines of the node 8 places ahead (`__builtin_prefetch`, `_mm_prefetch` on
  MSVC x86, a no-op elsewhere). Only addresses are computed; a prefetch
  never faults, so a node freed by an earlier callback is safe.
- Not done: moving the process fields into one line (a layout change for
  every node, smaller expected effect once the misses overlap); hoisting
  the tree's paused and suspended state out of the loop (the tree is in the
  cache, it does not show).
- A threshold (no prefetch under 4,096 nodes) was tried: the 1k
  difference stayed (+11%, 3 us), so it is code layout, not the
  prefetches, and the threshold was dropped.
