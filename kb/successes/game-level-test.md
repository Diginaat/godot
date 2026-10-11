---
type: Success
title: A real game level in the DDGI test suite
description: A storm-weather game level with a moving emissive object shows problems the synthetic test scenes don't, such as indirect light trailing a moving lamp.
tags: [success, testing, ddgi, visuals]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What worked

The user asked to always test DDGI changes in a real first-person game
level (a private project, not in this repository), in storm weather, with
a moving emissive object. Its screenshots showed the floor glow staying
where the sphere had been, which became the next improvement target.
Setup and caveats: [the test protocol](/tooling/ddgi-test-protocol.md#a-real-game-level).

# Reuse

Numbers from the synthetic scenes need a look at a real scene too. Keep
the real scene's test small: pinned exposure and frame rate, averages
over 3 runs, screenshots while moving and settled.
