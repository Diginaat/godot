#!/usr/bin/env python3
"""Runs the denoiser test suite: for every view, captures a reference
sequence (converged, no denoising), the path tracer's raw sequence and the
denoised sequence, and prints the metrics of both against the reference as a
Markdown table. See docs/renderer/native_ray_reconstruction.md.

Usage:
  run_suite.py --godot=<editor console exe> --out=<work dir> [--views=a,b]
               [--frames=30] [--reference=256] [--res=960x540] [--spp=1]
               [--keep-reference] [--extra="--scale3d=2 ..."]

--keep-reference reuses reference frames already in <work dir>/reference
(they only depend on the scene, not on the denoiser).
"""

import argparse
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metrics  # noqa: E402

VIEWS = ["cornell", "pan", "emissive", "skinned", "thin", "mirror", "lights", "dark"]
PROJECT = os.path.dirname(os.path.abspath(__file__))


def capture(godot, view, out_dir, a, extra_args):
    cmd = [
        godot,
        "--rendering-driver",
        "vulkan",
        "--path",
        PROJECT,
        "--",
        f"--view={view}",
        f"--res={a.res}",
        f"--spp={a.spp}",
        "--tonemap=linear",
        f"--frames={a.warmup}",
        f"--sequence={a.frames}",
        f"--out={out_dir}",
    ] + extra_args
    r = subprocess.run(cmd, capture_output=True, text=True)
    errors = [line for line in (r.stdout + r.stderr).splitlines() if "ERROR" in line or "SCRIPT ERROR" in line]
    if r.returncode != 0 or errors:
        print(f"  {view} {' '.join(extra_args)}: exit {r.returncode}", file=sys.stderr)
        for line in errors[:5]:
            print("   ", line, file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--views", default=",".join(VIEWS))
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--warmup", type=int, default=60)
    ap.add_argument("--reference", type=int, default=256)
    ap.add_argument("--res", default="960x540")
    ap.add_argument("--spp", type=int, default=1)
    ap.add_argument("--keep-reference", action="store_true")
    ap.add_argument("--extra", default="", help="More arguments for the denoised run.")
    ap.add_argument("--skip", type=int, default=0)
    a = ap.parse_args()

    ref_dir = os.path.join(a.out, "reference")
    raw_dir = os.path.join(a.out, "raw")
    den_dir = os.path.join(a.out, "denoised")
    extra = shlex.split(a.extra)
    rows = []
    for view in a.views.split(","):
        have_ref = a.keep_reference and len(metrics.frames(ref_dir, view)) >= a.frames
        if not have_ref:
            capture(a.godot, view, ref_dir, a, ["--denoiser=2", f"--reference={a.reference}"])
        capture(a.godot, view, raw_dir, a, ["--denoiser=2", "--rr_debug=4"])
        capture(a.godot, view, den_dir, a, ["--denoiser=2"] + extra)
        raw, _ = metrics.evaluate(raw_dir, ref_dir, view, a.skip)
        den, _ = metrics.evaluate(den_dir, ref_dir, view, a.skip)
        rows.append((view, raw, den))

    keys = ["rmse", "relmse", "ssim", "temporal", "flicker", "moving", "sharpness", "bias"]
    print("| View | " + " | ".join(keys) + " |")
    print("| --- |" + " --- |" * len(keys))
    for view, raw, den in rows:
        print(f"| {view} (raw) | " + " | ".join(f"{raw[k]:.4f}" for k in keys) + " |")
        print(f"| {view} | " + " | ".join(f"{den[k]:.4f}" for k in keys) + " |")


if __name__ == "__main__":
    main()
