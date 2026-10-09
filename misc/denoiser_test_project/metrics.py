#!/usr/bin/env python3
"""Image quality metrics for the native path tracer denoiser.

Compares a captured sequence (PNG frames from main.gd --sequence) against a
reference sequence of the same frames (--sequence --reference=K). Frames must
be captured with --tonemap=linear, so pixel values are linear radiance after
the sRGB transfer (decoded here).

Metrics (lower is better unless noted):
  rmse        root mean square error, linear
  relmse      mean of (d - r)^2 / (r^2 + 0.01): relative error, weighs dark areas
  psnr        dB on sRGB values (higher is better)
  ssim        structural similarity of luminance, 7x7 windows (higher is better)
  temporal    mean |(d_t - d_t-1) - (r_t - r_t-1)|: change between frames that
              the reference doesn't have (flicker, lag, ghost trails)
  flicker     mean |d_t - d_t-1| where the reference doesn't change
  moving      rmse where the reference changes (moving objects, light, shadows)
  sharpness   gradient on the reference's strongest edges (top 5%), test /
              reference (1 = as sharp, < 1 blurrier, > 1 noisier)
  bias        mean(test) / mean(reference) - 1 (energy gained or lost)

SSIM alone rewards blur, so read it together with sharpness and temporal.

Usage: metrics.py <test_dir> <reference_dir> [--prefix=view] [--skip=N]
Only numpy and Pillow are needed.
"""

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    return a


def srgb_to_linear(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def luminance(a):
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def box_filter(a, r):
    # Mean over (2r+1)^2 windows with an integral image (edges clamped).
    p = np.pad(a, r + 1, mode="edge").astype(np.float64)
    c = p.cumsum(0).cumsum(1)
    n = 2 * r + 1
    s = c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]
    return (s / (n * n))[: a.shape[0], : a.shape[1]]


def ssim(x, y, r=3):
    c1, c2 = 0.01**2, 0.03**2
    mx, my = box_filter(x, r), box_filter(y, r)
    vx = box_filter(x * x, r) - mx * mx
    vy = box_filter(y * y, r) - my * my
    cxy = box_filter(x * y, r) - mx * my
    s = ((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))
    return float(s.mean())


def gradient_image(a):
    gx = np.abs(np.diff(a, axis=1))[:-1, :]
    gy = np.abs(np.diff(a, axis=0))[:, :-1]
    return gx + gy


def edge_sharpness(test, ref):
    # Gradient on the reference's strongest edges (top 5%), where the
    # reference's leftover noise is small next to the edge itself.
    gt, gr = gradient_image(test), gradient_image(ref)
    edges = gr >= np.percentile(gr, 95)
    return float(gt[edges].mean() / max(gr[edges].mean(), 1e-6))


def frames(directory, prefix):
    return sorted(glob.glob(os.path.join(directory, prefix + "_*.png")))


def evaluate(test_dir, ref_dir, prefix, skip=0):
    test_files = frames(test_dir, prefix)
    ref_files = frames(ref_dir, prefix)
    n = min(len(test_files), len(ref_files))
    if n == 0:
        raise SystemExit(f"No frames for '{prefix}' in {test_dir} / {ref_dir}")
    acc = {k: [] for k in ("rmse", "relmse", "psnr", "ssim", "temporal", "flicker", "moving", "sharpness", "bias")}
    prev_d = prev_r = None
    for i in range(n):
        ds = load(test_files[i])
        rs = load(ref_files[i])
        d, r = srgb_to_linear(ds), srgb_to_linear(rs)
        if i >= skip:
            err = d - r
            acc["rmse"].append(float(np.sqrt((err**2).mean())))
            acc["relmse"].append(float(((err**2) / (r**2 + 0.01)).mean()))
            mse_s = float(((ds - rs) ** 2).mean())
            acc["psnr"].append(10 * np.log10(1.0 / max(mse_s, 1e-10)))
            acc["ssim"].append(ssim(luminance(ds), luminance(rs)))
            acc["sharpness"].append(edge_sharpness(luminance(ds), luminance(rs)))
            acc["bias"].append(float(d.mean() / max(r.mean(), 1e-6) - 1.0))
            if prev_d is not None:
                dd = d - prev_d
                dr = r - prev_r
                acc["temporal"].append(float(np.abs(dd - dr).mean()))
                change = luminance(np.abs(dr))
                still = change < 0.002
                if still.any():
                    acc["flicker"].append(float(luminance(np.abs(dd))[still].mean()))
                moving = change >= 0.01
                if moving.any():
                    acc["moving"].append(float(np.sqrt((luminance(err) ** 2)[moving].mean())))
        prev_d, prev_r = d, r
    return {k: (float(np.mean(v)) if v else float("nan")) for k, v in acc.items()}, n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("test_dir")
    ap.add_argument("reference_dir")
    ap.add_argument("--prefix", default="cornell")
    ap.add_argument("--skip", type=int, default=0, help="Frames to ignore at the start (warm-up).")
    a = ap.parse_args()
    m, n = evaluate(a.test_dir, a.reference_dir, a.prefix, a.skip)
    print(f"{a.prefix} frames={n} " + " ".join(f"{k}={v:.5f}" for k, v in m.items()))


if __name__ == "__main__":
    sys.exit(main())
