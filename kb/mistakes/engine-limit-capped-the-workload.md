---
type: Mistake
title: An engine limit silently capped the benchmark workload
description: Jolt's default max_bodies (10,240) created only a third of a 30k-body case; the run looked fast. Export-Csv also dropped every column the first row lacked.
tags: [mistake, benchmark, physics, powershell]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What happened

The first stock-compare physics run had Jolt at 10 ms per tick for 30k
bodies, faster than PhysX GPU. The log had `Failed to create underlying
Jolt Physics body ... Maximum number of bodies is currently set to 10240`:
only about 10k bodies existed; the rest stayed at their spawn points (mean
height 2.9 m instead of 1.0 m). In the same run, `Export-Csv` of rows with
different fields kept only the first row's columns, so the physics and
shader fields were missing from the CSV.

# Rule

* Check every run's log for `ERROR` lines before reading numbers, and
  print a sanity value that the workload existed (body count created,
  final state) next to the timing.
* Raise engine limits (Jolt `physics/jolt_physics_3d/limits/*`) to fit
  the largest case.
* Export one CSV per row shape (per suite), not one mixed table.
