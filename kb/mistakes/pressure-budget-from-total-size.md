---
type: Mistake
title: Streaming pressure budget sized from the total data, not from the views
description: A pool of 40% of the cooked cluster data still held both test views, so the pressure case never evicted; size pressure budgets from measured per-view needs.
tags: [testing, streaming, cluster-lod]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T12:00:00+01:00 }
---

# Observed failure

The CLOD streaming fixture set the pool to 40% of the cooked index bytes and
alternated two views, expecting evictions or deferred loads. On Vulkan it
recorded neither, and two of the views covered fewer pixels than the fixture's
minimum.

# Cause

The cooked size counts every level of every cluster hierarchy. A cut for a
view needs far less: here one view needed 212,280 bytes and both together
289,920 bytes, against 1,168,728 cooked bytes. Any fraction of the total that
looks "tight" can still hold the whole working set.

# Rule

Measure the working set first. The fixture now rebuilds the pool per view to
measure what each view needs alone, measures the union, and sets the budget
halfway between the larger single view and the union. Each view then fits, and
alternating them must evict (14 evictions in the passing run).
