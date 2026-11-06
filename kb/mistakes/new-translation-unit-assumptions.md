---
type: Mistake
title: Assumed APIs and includes in a new rendering translation unit
description: Native culling initially failed compilation because reused renderer types and helpers were not included or named as in this fork.
tags: [build, cpp, rendering]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# What happened

A new rendering source assumed a transitively visible MeshStorage declaration,
unqualified `next_power_of_2`, and a Vector-compatible uniform-cache overload.
The actual fork requires an explicit storage header, `Math::next_power_of_2`
and `LocalVector<RD::Uniform>`. First compile failed; the next passed after these
local corrections. No external SDK change was needed.

ArrayMesh attachment follow-up repeated the include assumption: adding a direct
`callable_mp` use required `core/object/callable_mp.h`, which mesh.cpp had not
previously included. Production compilation caught it; explicit include added.
Resident CLOD initially also called `version_set_compute_code` with a foreign
signature. Static compute shaders already use the version created from their
generated source; matching the existing MultiMesh setup needs no such call.
Scene import options are a HashMap, not a Dictionary: optional defaults require
`has()` plus indexed access, not Dictionary's two-argument `get()`. Check the
containing function signature as well as local dictionary examples.

# Rule

Check declarations and exact helper signatures in this fork before introducing
a new translation unit. Include directly used types, preserve established
container/API conventions, and compile the narrow new path early.
