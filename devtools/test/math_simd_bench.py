#!/usr/bin/env python3
"""Exploratory x86 float32 SIMD benchmark; no public math types are modified."""
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import statistics
import subprocess

from core_templates_bench import ensure_idle

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "devtools/out/math_simd_bench"
OUT.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
installation = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
source = ROOT / "devtools/test/math_simd_bench.cpp"
exe = OUT / "bench.exe"
args = ["cl", "/nologo", "/O2", "/MT", "/std:c++17", "/Zc:__cplusplus", "/fp:precise", "/DNOMINMAX", "/DWINDOWS_ENABLED", f"/I{ROOT}", f"/I{ROOT / 'platform/windows'}", f"/Fo{OUT / 'bench.obj'}", f"/Fe{exe}", str(source)]
command = OUT / "build.cmd"
command.write_text(f'@call "{vcvars}" >nul\n@if errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(args) + "\n@exit /b %errorlevel%\n", encoding="utf-8")
build = subprocess.run(["cmd", "/d", "/c", str(command)], cwd=ROOT, text=True, capture_output=True)
(OUT / "build.log").write_text(build.stdout + build.stderr, encoding="utf-8")
if build.returncode:
    raise RuntimeError(build.stdout + build.stderr)
ensure_idle()
result = subprocess.run([str(exe)], cwd=ROOT, check=True, text=True, capture_output=True, timeout=120)
(OUT / "raw.csv").write_text(result.stdout, encoding="utf-8")
rows = list(csv.DictReader(io.StringIO(result.stdout)))
if len(rows) != 77:
    raise RuntimeError(f"Expected 77 timing rows, got {len(rows)}")
summary = []
for case in dict.fromkeys(r["case"] for r in rows):
    times = sorted(float(r["ns_per_vector"]) for r in rows if r["case"] == case)
    if len(times) != 11 or not all(t > 0 for t in times):
        raise RuntimeError("Invalid timings")
    median = statistics.median(times)
    summary.append({"case": case, "p50_ns": median, "p95_ns": times[-1], "mad_pct": statistics.median(abs(t - median) for t in times) / median * 100})
metadata = {"cpu": subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"], text=True).strip(), "flags": args[2:9], "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "vector3_sha256": hashlib.sha256((ROOT / 'core/math/vector3.h').read_bytes()).hexdigest(), "scope": "finite float32 inputs only; no ABI changes; no NEON validation; explicit aligned SIMD loads only; no FMA or approximate reciprocals", "vectors": 16384, "repeats": 128, "warmups": 1, "samples": 11}
(OUT / "results.json").write_text(json.dumps({"metadata": metadata, "summary": summary}, indent=2), encoding="utf-8")
for row in summary:
    print(f"{row['case']:25} p50={row['p50_ns']:.3f} ns/vector p95={row['p95_ns']:.3f} MAD={row['mad_pct']:.2f}%")
print(f"Artifacts: {OUT}")
