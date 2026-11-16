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
- Commit, update this table and the memory file, push the branch.

| # | Milestone | Measures | Tests | Rollback if | State |
| --- | --- | --- | --- | --- | --- |
| M0 | Phase 0: analysis, scenario benchmark, baseline | all 12 scenarios, 1k/10k/100k, editor and release | benchmark runs without errors | - | done 2026-11-16 |
| M1 | `SceneTree::add_to_group`: the linear duplicate check only in dev builds (both callers already guarantee a new member) | `churn`, `instance`, enter/exit with groups | group membership and order through add/remove/re-enter | any scenario slower | done: `instance` 100k -60%, `churn` 100k -51% |
| M2 | Children cache: a removal leaves a hole, the next reader compacts it in place (no walk of the name map, no sort); updating it on every removal would cost more when many removals come before one read | `churn`, `reparent`, wide parents | child order, `get_index()`, internal front/back children, `move_child` after removals (test in place) | `wide`/`churn` not faster, or removals of front children much slower | done: `churn_read` -15-17%, profiled node workload -6.4% |
| M3 | `remove_from_group` without a linear search (member index or tree-order rules) | `churn`, `instance` | `get_nodes_in_group` order, `call_group` during removal | group order changes | done: `churn` 100k -81%, `instance` 100k -70%, 10k -17-39% |
| M4 | Node3D transform propagation stops at subtrees that need nothing more (per-subtree count of nodes that want `TRANSFORM_CHANGED`, plus the dirty invariant) | `deep`, `parents`, `heavy`, `-Visual` | notification counts per frame, global transforms after random moves, top_level, reparent, FTI | any notification lost or delayed | next (design below: stop at dirty children, notified nodes cleaned) |
| M5 | Allocations per child, group membership and signal connection (pooled or inline storage where the profile shows them) | `instance`, `churn`, allocation counts | lifecycle, ownership, signals | memory up without time down | |
| M6 | Process lists: incremental priority order, cheaper per-node checks (only if a script-free profile shows them) | `native`, `script` | process order with priorities and pause modes | callback order changes | |
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
