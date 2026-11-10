# Phase 5 — ARM NEON (blocked)

Start only after a stable, accepted x86 component and shared behavioral tests.
No production x86 component is accepted, so no NEON implementation starts now.

Target AArch64 first; preserve public layouts and common high-level logic.
Select native efficient NEON algorithms, not instruction-by-instruction SSE
translation. Separate platform source/ISA selection through SCons.

Gate: compile with ARM toolchains; float/double and exceptional-value parity;
aligned access proof; ARM scalar/batch benchmarks on hardware. Without hardware,
mark runtime/performance pending. No ARM speedup claim from static validation.
