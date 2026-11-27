---
type: Mistake
title: A benchmark case run alone hid a per-frame cost that showed up after another case
description: The first version of the previous transform table (OD12) cost dynamic_20k 5% GPU time when it ran after small_1k in one process, and nothing when it ran alone. The cause was real (entries that stayed spread over the whole table), not noise; the alone run only happened not to show it.
tags: [mistake, measuring, gpu-instances, vram]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What happened

The instance bench ran `small_1k,dynamic_20k,massive_100k,churn_100k`
in one process. With the new table, `dynamic_20k` was 0.694 to 0.730 ms
GPU and +3.4 MB of video memory. Rerun with only `dynamic_20k`, both
builds gave 0.720-0.727 ms. That looked like a sequence artifact.

Rerun with `small_1k,dynamic_20k`, four rounds: still +0.035 ms GPU and
+0.04 ms render CPU, every round. A temporary counter showed 2,000 live
entries spread up to 19,991: every instance gets an entry on its
creation frame, the static ones free theirs, and the moving ones keep
high entries. Each frame then uploaded a 960 KB span. Compaction fixed
it (2,000 entries in 2,048).

# Why

The first rerun changed two things at once (the case order and the
process state) and was read as "no difference" instead of "a different
condition". The size of the span did not depend on the order. The
alone run's baseline just landed at a different level.

# Rule

- When a case regresses only in its usual sequence, rerun that sequence
  (several rounds) before calling it an artifact; "alone" is another
  measurement, not a check of the first.
- For a new allocator or table, print its live count, highest entry and
  upload size during the bench once; fragmentation shows there at once.
