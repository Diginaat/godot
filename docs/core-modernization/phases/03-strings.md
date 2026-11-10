# Phase 3 — strings (planned)

Actual header: core/string/ustring.h. UTF-32 String is not std::string. Current
prototype retains packed COW and uses char_traits; no STL storage adoption accepted.

Baseline construction, concat, compare, find, UTF conversions, move/copy and
temporary allocation counts; include StringName integration and encoded buffers.
Investigate string_view only where borrowed lifetime is explicit. Compare
basic_string storage without assuming equivalent sharing or pointer semantics.

Gate: Unicode/terminators/aliasing/lifetimes, serialization, scripting, managed
and extension round trips, measured useful workload benefit and separate commit.
