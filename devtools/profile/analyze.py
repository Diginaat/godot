#!/usr/bin/env python3
"""Summaries of a sampler.exe profile (devtools/profile/README.md).

  analyze.py <profile.tsv> summary [--threads N] [--top N]
      Busy samples per thread, by the leaf's source directory, and the
      core/templates, core/string and core/math functions and types.
  analyze.py <profile.tsv> callers <regex> [--skip dir,dir] [--top N]
      Busiest thread: samples whose leaf function matches, by the first
      caller outside core/templates, core/string, system code and --skip.

Waits (system wait, sleep and message functions in ntdll and win32u) count
as idle: the sampler cannot tell a waiting thread from a working one by its
cycle counter alone. Locks and other system calls count as work.
"""

import argparse
import re
from collections import Counter, defaultdict

FOCUS = ("core/templates", "core/string", "core/math")
IDLE_MODULES = ("ntdll", "win32u")
IDLE_FUNCTIONS = re.compile(r"Wait|Delay|Yield|Sleep|RemoveIoCompletion|GetMessage|MsgWait|SignalAndWait|AlertThread")


def is_idle(module, segments):
    return module.lower() in IDLE_MODULES and bool(IDLE_FUNCTIONS.search(segments[0][0].split("@")[0]))
TOP_DIRS = r"/(core|scene|servers|drivers|modules|platform|editor|thirdparty|main|tests)/"


def load(path):
    names, rows = {}, []
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "#thread":
            names[parts[1]] = parts[2] if len(parts) > 2 else ""
            continue
        segments, current = [], []
        for field in parts[3:]:
            if field.startswith("|"):
                segments.append(current)
                current = [field[1:]] if field[1:] else []
            else:
                current.append(field)
        segments.append(current)
        rows.append((int(parts[0]), parts[1], parts[2], [s for s in segments if s]))
    return names, rows


def split_frame(frame):
    m = re.match(r"(.*)@(.*):(\d+)$", frame)
    return (m.group(1), m.group(2).replace("\\", "/")) if m else (frame, "")


def relative(file):
    m = re.search(TOP_DIRS, file)
    return file[m.start() + 1:] if m else ""


def category(module, frame):
    function, file = split_frame(frame)
    rel = relative(file)
    if rel:
        parts = rel.split("/")
        return ("/".join(parts[:2]) if len(parts) > 2 else parts[0]), function
    lower = file.lower()
    if "/crt/" in lower or "vctools" in lower or "/msvc/" in lower or "ucrt" in lower:
        return "crt", function
    if module.lower().startswith("godot"):
        return "godot (no line)", function
    return "dll:" + module.lower(), function


def strip_templates(function):
    depth, out = 0, ""
    for ch in function.split("@")[0]:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out += ch
    return out


def busiest(rows, names):
    busy = Counter()
    for count, tid, module, segments in rows:
        if not is_idle(module, segments):
            busy[tid] += count
    return busy


def summary(names, rows, args):
    busy = busiest(rows, names)
    grand = sum(busy.values())
    print(f"busy samples {grand}")
    for tid, total in busy.most_common(args.threads):
        dirs, types, functions = Counter(), Counter(), Counter()
        for count, t, module, segments in rows:
            if t != tid or is_idle(module, segments):
                continue
            cat, function = category(module, segments[0][0])
            dirs[cat] += count
            if cat in FOCUS:
                types[(cat, re.split(r"[<:]", function, maxsplit=1)[0])] += count
                functions[(cat, strip_templates(function))] += count
        print(f"\n== {names.get(tid) or 'tid ' + tid}: {total} samples ({100.0 * total / grand:.1f}%)")
        for cat, count in dirs.most_common(args.top):
            print(f"  {100.0 * count / total:5.1f}%  {cat}")
        focus = sum(count for (cat, _), count in types.items())
        print(f"  -- {' + '.join(FOCUS)}: {100.0 * focus / total:.1f}%; by type:")
        for (cat, base), count in types.most_common(args.top):
            print(f"  {100.0 * count / total:5.2f}%  {cat:15} {base}")
        print("  by function:")
        for (cat, function), count in functions.most_common(args.top):
            print(f"  {100.0 * count / total:5.2f}%  {cat:15} {function[:90]}")


def callers(names, rows, args):
    pattern = re.compile(args.pattern)
    skip = [s for s in args.skip.split(",") if s]
    busy = busiest(rows, names)
    tid, total = busy.most_common(1)[0]

    def outside(frame):
        function, file = split_frame(frame)
        rel = relative(file)
        return bool(rel) and not any(rel.startswith(d) for d in ["core/templates", "core/string"] + skip)

    matched, leaves, by_caller = 0, Counter(), Counter()
    for count, t, module, segments in rows:
        if t != tid:
            continue
        leaf = strip_templates(segments[0][0])
        if not pattern.search(leaf):
            continue
        matched += count
        leaves[leaf] += count
        frames = [f for s in segments for f in s]
        by_caller[next((strip_templates(f) for f in frames if outside(f)), "?")] += count
    print(f"{names.get(tid) or 'tid ' + tid}: {total} busy samples; leaves matching {args.pattern!r}: {100.0 * matched / total:.2f}%")
    for leaf, count in leaves.most_common(6):
        print(f"  leaf {100.0 * count / total:5.2f}%  {leaf[:90]}")
    for caller, count in by_caller.most_common(args.top):
        print(f"  {100.0 * count / total:5.2f}%  <- {caller[:100]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile")
    sub = parser.add_subparsers(dest="mode", required=True)
    s = sub.add_parser("summary")
    s.add_argument("--threads", type=int, default=3)
    s.add_argument("--top", type=int, default=12)
    c = sub.add_parser("callers")
    c.add_argument("pattern")
    c.add_argument("--skip", default="core/os")
    c.add_argument("--top", type=int, default=16)
    args = parser.parse_args()
    names, rows = load(args.profile)
    (summary if args.mode == "summary" else callers)(names, rows, args)


if __name__ == "__main__":
    main()
