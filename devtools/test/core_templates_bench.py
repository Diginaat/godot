#!/usr/bin/env python3
"""Controlled MSVC microbench: actual HEAD vs working-tree Godot containers.

No checkout, reset, project import or engine-binary replacement. Outputs stay in
devtools/out/core_templates_bench (ignored). Build while other work runs; measure
only after compilers and other benchmarks stop. Not a whole-engine benchmark.
"""

import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    "core/templates/vector.h",
    "core/templates/cowdata.h",
    "core/templates/a_hash_map.h",
    "core/templates/hash_map.h",
    "core/templates/hash_set.h",
    "core/templates/hashfuncs.h",
    "core/templates/hashfuncs.cpp",
    "core/string/ustring.h",
    "core/string/ustring.cpp",
]


def run(command, **kwargs):
    return subprocess.run(command, cwd=ROOT, check=True, text=True, capture_output=True, **kwargs)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(out, baseline_ref):
    if not (ROOT / "core/profiling/profiling.gen.h").is_file():
        raise RuntimeError("Missing SCons-generated profiling header. Configure/build Godot with SCons first.")
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = run([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"]).stdout.strip()
    vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
    if not vcvars.is_file():
        raise RuntimeError("MSVC vcvars64.bat not found")
    commit = run(["git", "rev-parse", baseline_ref]).stdout.strip()
    baseline = out / "baseline"
    for relative in SOURCES:
        target = baseline / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        content = subprocess.run(["git", "show", f"{commit}:{relative}"], cwd=ROOT, check=True, capture_output=True).stdout
        target.write_bytes(content)

    manifest = {"baseline_commit": commit, "flags": "/O2 /MT /Gy /Gw /std:c++17 /Zc:__cplusplus /GR- /DDEBUG_ENABLED /DTOOLS_ENABLED /DWINDOWS_ENABLED /OPT:REF /OPT:ICF", "sources": {}}
    support = ROOT / "devtools/test/core_templates_bench.cpp"
    for variant in ("baseline", "candidate"):
        directory = out / variant
        directory.mkdir(parents=True, exist_ok=True)
        include = baseline if variant == "baseline" else ROOT
        source = baseline if variant == "baseline" else ROOT
        # Do not link the entire String TU (it references Variant, translation,
        # crypto and OS services). Compile only the two exact, unmodified method
        # definitions used here. Storage/copy/detach operations come from the real
        # String header, not reimplementations. Fail if extraction ever drifts.
        string_source = source / "core/string/ustring.cpp"
        text = string_source.read_text(encoding="utf-8")
        methods = []
        for signature in ("bool String::operator==(const String &p_str) const", "uint32_t String::hash() const"):
            found = re.findall(r"^" + re.escape(signature) + r" \{\n.*?^\}", text, re.MULTILINE | re.DOTALL)
            if len(found) != 1:
                raise RuntimeError(f"Cannot extract exact String method: {signature}")
            methods.append(found[0])
        extracted = directory / "string_methods.cpp"
        extracted.write_text('#include "core/string/ustring.h"\n\n' + "\n\n".join(methods) + "\n", encoding="utf-8")
        inputs = [support, ROOT / "core/os/memory.cpp", extracted, source / "core/templates/hashfuncs.cpp"]
        manifest["sources"][variant] = {str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path): sha(path) for path in inputs}
        manifest["sources"][variant][str(string_source.relative_to(ROOT))] = sha(string_source)
        manifest["sources"][variant].update({name: sha(include / name) for name in SOURCES if (include / name).is_file()})
        if variant == "candidate":
            for name in ["core/templates/flat_shared_buffer.h", "core/templates/robin_hood_hash_index.h", "thirdparty/robin_hood/robin_hood.h", "thirdparty/wyhash/wyhash.h", "thirdparty/xxhash/xxhash.h"]:
                if (ROOT / name).is_file():
                    manifest["sources"][variant][name] = sha(ROOT / name)
        manifest["sources"][variant]["core/profiling/profiling.gen.h"] = sha(ROOT / "core/profiling/profiling.gen.h")
        compile_args = [
            "cl", "/nologo", "/O2", "/MT", "/Gy", "/Gw", "/std:c++17", "/Zc:__cplusplus", "/GR-",
            "/DDEBUG_ENABLED", "/DTOOLS_ENABLED", "/DWINDOWS_ENABLED", "/DNOMINMAX", "/D_CRT_SECURE_NO_WARNINGS",
            f"/DBENCH_MODERN_HASH={int('uint64_t hash_buffer64(' in (source / 'core/templates/hashfuncs.h').read_text(encoding='utf-8'))}", f"/I{include}", f"/I{ROOT}", f"/I{ROOT / 'platform/windows'}",
            f"/Fo{directory}\\", f"/Fe{directory / 'bench.exe'}", *map(str, inputs),
            "/link", "/OPT:REF", "/OPT:ICF", "/INCREMENTAL:NO",
        ]
        command = directory / "build.cmd"
        command.write_text(f'@call "{vcvars}" >nul\n@if errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(compile_args) + "\n@exit /b %errorlevel%\n", encoding="utf-8")
        print(f"Building {variant}", flush=True)
        result = subprocess.run(["cmd", "/d", "/c", str(command)], cwd=ROOT, text=True, capture_output=True)
        (out / f"{variant}_build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{variant} build failed; see {out / f'{variant}_build.log'}\n{result.stdout[-6000:]}\n{result.stderr[-2000:]}")
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def ensure_idle():
    # Avoid reporting compile contention as a container regression. Never kill processes.
    query = (
        "Get-CimInstance Win32_Process | Where-Object { "
        "$_.Name -in @('cl.exe', 'link.exe', 'clang.exe', 'clang-cl.exe') -or "
        "($_.Name -like 'python*' -and $_.CommandLine -match '(?i)\\bSCons\\b') "
        "} | Select-Object -ExpandProperty Name"
    )
    active = subprocess.run(["powershell", "-NoProfile", "-Command", query], capture_output=True, text=True).stdout.strip()
    if active:
        raise RuntimeError("Compiler/linker still active. Stop other builds before measurement; no timing samples collected.")


def collect(executable):
    result = run([str(executable)], timeout=180)
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    if len(rows) != 24:
        raise RuntimeError(f"Expected 24 cases, got {len(rows)}\n{result.stdout}\n{result.stderr}")
    for row in rows:
        if not math.isfinite(float(row["ns_per_op"])) or float(row["ns_per_op"]) <= 0 or int(row["operations"]) <= 0:
            raise RuntimeError(f"Invalid timing: {row}")
    return rows


def measure(out, samples):
    ensure_idle()
    manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    for name, expected in manifest["sources"]["candidate"].items():
        path = ROOT / name
        if sha(path) != expected:
            raise RuntimeError(f"Candidate source changed after build: {name}")
    raw = []
    # One complete pair warms code/allocator caches; excluded from summary.
    for sample in range(-1, samples):
        ensure_idle()
        order = ["baseline", "candidate"] if sample % 2 else ["candidate", "baseline"]
        pair = {}
        for variant in order:
            print(f"{'Warmup' if sample < 0 else f'Sample {sample + 1}/{samples}'} {variant}", flush=True)
            pair[variant] = collect(out / variant / "bench.exe")
        for a, b in zip(pair["baseline"], pair["candidate"]):
            if a["case"] != b["case"] or a["operations"] != b["operations"]:
                raise RuntimeError("Workload mismatch")
            if not a["case"].startswith("hash_bytes_") and a["checksum"] != b["checksum"]:
                raise RuntimeError(f"Cross-variant correctness mismatch: {a} != {b}")
        if sample >= 0:
            for variant in order:
                raw.extend({"sample": sample, "variant": variant, **row} for row in pair[variant])
    with (out / "raw.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(raw[0]))
        writer.writeheader()
        writer.writerows(raw)
    summary = []
    for case in dict.fromkeys(row["case"] for row in raw):
        row = {"case": case}
        for variant in ("baseline", "candidate"):
            subset = [r for r in raw if r["case"] == case and r["variant"] == variant]
            times = sorted(float(r["ns_per_op"]) for r in subset)
            if len({(r["operations"], r["live_bytes"], r["checksum"]) for r in subset}) != 1:
                raise RuntimeError(f"Non-repeatable workload or allocation size: {case}/{variant}")
            median = statistics.median(times)
            row[f"{variant}_p50_ns"] = median
            row[f"{variant}_p95_ns"] = times[math.ceil(len(times) * .95) - 1]
            row[f"{variant}_mad_pct"] = statistics.median(abs(t - median) for t in times) / median * 100
            row[f"{variant}_live_bytes"] = int(subset[0]["live_bytes"])
        row["candidate_over_baseline"] = row["candidate_p50_ns"] / row["baseline_p50_ns"]
        summary.append(row)
    with (out / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    metadata = {"samples_per_variant": samples, "warmup_pairs": 1, "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "baseline_commit": manifest["baseline_commit"], "cpu": run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"]).stdout.strip(), "flags": manifest["flags"], "memory_metric": "live requested Godot allocation bytes; not CRT heap overhead, RSS or peak bytes", "scope": "standalone containers with real memory/string/hash sources and fail-fast diagnostic stubs; not whole-engine performance"}
    (out / "results.json").write_text(json.dumps({"metadata": metadata, "summary": summary}, indent=2), encoding="utf-8")
    print("\ncase                               baseline ns  candidate ns  ratio   live bytes B -> C")
    for row in summary:
        print(f"{row['case']:34} {row['baseline_p50_ns']:11.2f} {row['candidate_p50_ns']:13.2f} {row['candidate_over_baseline']:6.2f}x  {row['baseline_live_bytes']} -> {row['candidate_live_bytes']}")
    print(f"\nRaw samples, summary and manifest: {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="HEAD")
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--out", type=Path, default=ROOT / "devtools/out/core_templates_bench")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if args.samples < 5:
        parser.error("Use at least five samples per variant")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        build(args.out, args.baseline)
    if not args.build_only:
        measure(args.out, args.samples)
