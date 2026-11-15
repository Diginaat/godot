#!/usr/bin/env python3
"""Generate an isolated STL-copy experiment; never modify the engine header."""
import argparse
from pathlib import Path

from core_templates_bench import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Header destination below devtools/out/unique_array_bench")
    parser.add_argument("--policy", choices=("stl", "scalar-single"), default="stl")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "devtools/out/unique_array_bench").resolve()):
        parser.error("Experimental headers must stay below devtools/out/unique_array_bench")
    source = (ROOT / "core/templates/local_vector.h").read_text(encoding="utf-8")
    start = source.index("\texplicit LocalVector(Span<T> p_span) {")
    end = source.index("\texplicit LocalVector(const LocalVector &p_vector)", start)
    candidate = """\texplicit LocalVector(Span<T> p_span) {
\t\treserve(p_span.size());
\t\tif constexpr (std::is_trivially_copyable_v<T>) {
\t\t\tif (p_span.size()) {
\t\t\t\tstd::uninitialized_copy_n(p_span.ptr(), p_span.size(), data);
\t\t\t}
\t\t} else {
\t\t\tfor (size_t i = 0; i < p_span.size(); i++) {
\t\t\t\tmemnew_placement(&data[i], T(p_span.ptr()[i]));
\t\t\t}
\t\t}
\t\tcount = p_span.size();
\t}
"""
    if args.policy == "scalar-single":
        # Exact-size singleton avoids an out-of-line library copy call. Empty
        # spans need neither allocation nor copying. This is an experiment,
        # not a measured engine policy.
        candidate = candidate.replace("\t\treserve(p_span.size());", "\t\tif (!p_span.size()) {\n\t\t\treturn;\n\t\t}\n\t\treserve(p_span.size());", 1)
        candidate = candidate.replace(
            "\t\t\tif (p_span.size()) {\n\t\t\t\tstd::uninitialized_copy_n(p_span.ptr(), p_span.size(), data);",
            "\t\t\tif (p_span.size() == 1) {\n\t\t\t\tmemnew_placement(data, T(*p_span.ptr()));\n\t\t\t} else {\n\t\t\t\tcopy_arr_placement(data, p_span.ptr(), p_span.size());",
            1,
        )
    source = source[:start] + candidate + source[end:]
    if args.policy == "stl":
        source = source.replace("#include <initializer_list>", "#include <initializer_list>\n#include <memory>", 1)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(source, encoding="utf-8", newline="\n")
    print(f"Generated isolated {args.policy} candidate: {output}")


if __name__ == "__main__":
    main()
