#!/usr/bin/env python3
"""Baseline-only LocalVector/STL allocator experiment. No engine files edited."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tarfile

from core_templates_bench import ROOT, ensure_idle

BASELINE = "172e099b95307708005dafcf719693d20b1e7088"
OUT = ROOT / "devtools/out/unique_array_bench"


def checked(args, **kwargs):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def compare_copy():
    for extra in ([], ["--candidate-local"]):
        print(checked([sys.executable, str(Path(__file__).resolve()), "--copy-only", "--samples", "1", *extra]), end="")
    directory = OUT / "copy"
    raw = []
    for sample in range(11):
        ensure_idle()
        order = ("baseline", "candidate") if sample % 2 else ("candidate", "baseline")
        for variant in order:
            rows = list(csv.DictReader(io.StringIO(checked([str(directory / variant / "timing/bench.exe")], timeout=120))))
            if len(rows) != 14:
                raise RuntimeError("Invalid copy comparison rows")
            raw.extend({"pair": sample, "build": variant, **row} for row in rows)
    with (directory / "paired.csv").open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(raw[0]))
        writer.writeheader()
        writer.writerows(raw)
    summary = []
    for n in (0, 1, 16, 256, 4096, 32768, 1048576):
        row = {"n": n}
        sums = set()
        for variant in ("baseline", "candidate"):
            for implementation in ("local", "stl"):
                values = [r for r in raw if int(r["n"]) == n and r["build"] == variant and r["variant"] == implementation]
                sums.update(r["checksum"] for r in values)
                times = sorted(float(r["ns_per_cycle"]) for r in values)
                median = statistics.median(times)
                row[f"{variant}_{implementation}"] = {"p50_ns": median, "p95_ns": times[-1], "mad_pct": statistics.median(abs(t - median) for t in times) / median * 100}
        if len(sums) != 1:
            raise RuntimeError("Before/after checksum mismatch")
        row["ratio"] = row["candidate_local"]["p50_ns"] / row["baseline_local"]["p50_ns"]
        summary.append(row)
        print(f"COPY n={n:7} {row['baseline_local']['p50_ns']:.2f} -> {row['candidate_local']['p50_ns']:.2f} ns/copy ratio={row['ratio']:.3f}; STL control {row['baseline_stl']['p50_ns']:.2f} -> {row['candidate_stl']['p50_ns']:.2f}")
    (directory / "paired_results.json").write_text(json.dumps({"pairs": 11, "warmup_per_process": 1, "summary": summary}, indent=2), encoding="utf-8")


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-local", action="store_true", help="Overlay only working-tree local_vector.h on original baseline headers")
    parser.add_argument("--copy-only", action="store_true")
    parser.add_argument("--compare-copy", action="store_true")
    parser.add_argument("--samples", type=int, default=11)
    args = parser.parse_args()
    if args.compare_copy:
        compare_copy()
        return
    if args.samples < 1:
        parser.error("At least one sample required")
    if args.copy_only:
        OUT = OUT / "copy" / ("candidate" if args.candidate_local else "baseline")
    elif args.candidate_local:
        OUT = OUT / "candidate"
    OUT.mkdir(parents=True, exist_ok=True)
    baseline = OUT / "sources" / BASELINE
    if not baseline.exists():
        baseline.mkdir(parents=True)
        archive = subprocess.check_output(["git", "archive", BASELINE, "core", "platform/windows/platform_config.h", "thirdparty/misc"], cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(archive)) as files:
            files.extractall(baseline, filter="data")
    generated = ROOT / "core/profiling/profiling.gen.h"
    if not generated.is_file():
        raise RuntimeError("Configure Godot via SCons first: profiling.gen.h is required")
    (baseline / "core/profiling/profiling.gen.h").write_bytes(generated.read_bytes())
    if args.candidate_local:
        (baseline / "core/templates/local_vector.h").write_bytes((ROOT / "core/templates/local_vector.h").read_bytes())
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = checked([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]).strip()
    vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
    source = ROOT / "devtools/test/unique_array_bench.cpp"
    header = ROOT / "devtools/test/array_bench_counters.h"
    # Only memory.cpp is force-included with hooks. Standard-library headers and
    # the harness never see allocation macros. Timing binary has no hooks.
    for mode in ("timing", "counts"):
        directory = OUT / mode
        directory.mkdir(exist_ok=True)
        flags = ["cl", "/nologo", "/O2", "/MT", "/std:c++17", "/Zc:__cplusplus", "/DDEBUG_ENABLED", "/DWINDOWS_ENABLED", f"/I{baseline}", f"/I{baseline / 'platform/windows'}"]
        commands = [
            flags + ["/c", f"/FI{header}", f"/DARRAY_BENCH_HOOKS={int(mode == 'counts')}", f"/Fo{directory / 'memory.obj'}", str(baseline / "core/os/memory.cpp")],
            flags + [f"/DARRAY_BENCH_COPY_ONLY={int(args.copy_only)}", f"/DARRAY_BENCH_SAMPLES={1 if mode == 'counts' else args.samples}", f"/Fo{directory / 'bench.obj'}", f"/Fe{directory / 'bench.exe'}", str(source), str(directory / "memory.obj")],
        ]
        script = directory / "build.cmd"
        text = f'@call "{vcvars}" >nul\n@if errorlevel 1 exit /b %errorlevel%\n'
        text += "\n@if errorlevel 1 exit /b %errorlevel%\n".join(subprocess.list2cmdline(command) for command in commands)
        script.write_text(text + "\n@exit /b %errorlevel%\n", encoding="utf-8")
        (OUT / f"{mode}_build.log").write_text(checked(["cmd", "/d", "/c", str(script)]), encoding="utf-8")
    ensure_idle()
    outputs = {}
    for mode in ("timing", "counts"):
        output = checked([str(OUT / mode / "bench.exe")], timeout=240)
        (OUT / f"{mode}.csv").write_text(output, encoding="utf-8")
        outputs[mode] = list(csv.DictReader(io.StringIO(output)))
    cases = 7 if args.copy_only else 56
    if len(outputs["timing"]) != cases * 2 * args.samples or len(outputs["counts"]) != cases * 2:
        raise RuntimeError("Wrong workload/sample counts")
    summary = []
    for n in (0, 1, 16, 256, 4096, 32768, 1048576):
        for case in dict.fromkeys(r["case"] for r in outputs["timing"]):
            row = {"n": n, "case": case}
            checksums = []
            for variant in ("local", "stl"):
                measured = [r for r in outputs["timing"] if int(r["n"]) == n and r["case"] == case and r["variant"] == variant]
                counts = next(r for r in outputs["counts"] if int(r["n"]) == n and r["case"] == case and r["variant"] == variant)
                if len({r["checksum"] for r in measured}) != 1 or measured[0]["checksum"] != counts["checksum"]:
                    raise RuntimeError("Counter/timing workload mismatch")
                checksums.append(counts["checksum"])
                times = sorted(float(r["ns_per_cycle"]) for r in measured)
                median = statistics.median(times)
                row[variant] = {"p50_ns": median, "p95_ns": times[-1], "mad_pct": statistics.median(abs(t - median) for t in times) / median * 100, **{k: int(counts[k]) for k in ("repeats", "allocations", "reallocations", "frees", "crt_peak_delta", "live_requested_bytes", "capacity")}}
            if checksums[0] != checksums[1]:
                raise RuntimeError("Cross-container checksum mismatch")
            row["ratio"] = row["stl"]["p50_ns"] / row["local"]["p50_ns"]
            summary.append(row)
    inputs = [source, header, baseline / "core/os/memory.cpp", baseline / "core/templates/local_vector.h", baseline / "core/templates/vector.h", baseline / "core/templates/cowdata.h", generated]
    metadata = {"baseline": BASELINE, "candidate_local": args.candidate_local, "samples": args.samples, "copy_only": args.copy_only, "warmups": 1, "compiler_flags": "MSVC /O2 /MT C++17 DEBUG_ENABLED; no LTO; uninstrumented timing, separate CRT-hook counts", "cpu": checked(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"]).strip(), "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, "memory": "live_requested_bytes from Godot; crt_peak_delta is CRT _msize usable bytes including engine allocation headers; counts exclude prepared fixtures and include all repeated cycles"}
    (OUT / "results.json").write_text(json.dumps({"metadata": metadata, "summary": summary}, indent=2), encoding="utf-8")
    for row in summary:
        if row["n"] in (16, 32768, 1048576):
            print(f"n={row['n']:7} {row['case']:20} local={row['local']['p50_ns']:10.2f} stl={row['stl']['p50_ns']:10.2f} ns/cycle ratio={row['ratio']:.2f} alloc/realloc={row['local']['allocations']}/{row['local']['reallocations']} -> {row['stl']['allocations']}/{row['stl']['reallocations']}")
    print(f"PASS: contracts, {cases} paired workloads and counter/timing checksum parity. Artifacts: {OUT}")


if __name__ == "__main__":
    main()
