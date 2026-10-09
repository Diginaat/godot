"""Image measurements for the DDGI tests (devtools/test/ddgi/ddgi_tests.ps1).

  python ddgi_eval.py switch DIR     light switch: difference of each shot to the last one
  python ddgi_eval.py scroll A B     moving shot A against settled shot B
  python ddgi_eval.py weather DIR [N] storm: patchiness and lag (N = storm frames)
  python ddgi_eval.py grid OUT IMG... a 4-column contact sheet of the images

Needs Pillow and NumPy (python -m pip install pillow numpy).
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def gray(path):
    return np.asarray(Image.open(path).convert("L"), dtype=np.float32)


def box(a, r):
    k = np.ones(2 * r + 1) / (2 * r + 1)
    a = np.apply_along_axis(lambda m: np.convolve(np.pad(m, r, mode="edge"), k, "valid"), 0, a)
    return np.apply_along_axis(lambda m: np.convolve(np.pad(m, r, mode="edge"), k, "valid"), 1, a)


def switch(d):
    shots = sorted(glob.glob(os.path.join(d, "s_*.png")), key=lambda p: int(os.path.basename(p)[2:-4]))
    final = gray(shots[-1])
    print(f"final ({os.path.basename(shots[-1])}) mean {final.mean():.1f}")
    for p in shots[:-1]:
        diff = np.abs(gray(p) - final)
        print(f"  {os.path.basename(p):>10}: mean {gray(p).mean():6.1f}  difference {diff.mean():5.2f}  pixels off by >16: {100 * (diff > 16).mean():5.1f}%")


def scroll(a, b):
    diff = np.abs(gray(a) - gray(b))
    print(f"mean error {diff.mean():.2f}  pixels off by >16: {100 * (diff > 16).mean():.1f}%")


def weather(d, n=120):
    files = sorted(glob.glob(os.path.join(d, "f*.png")))
    final = gray(files[-1])
    patch = []
    prev = gray(files[0])
    for f in files[1:n + 40]:
        cur = gray(f)
        diff = cur - prev
        patch.append(np.abs(diff - box(diff, 12)).mean())
        prev = cur
    lag = next((i for i, f in enumerate(files[n:]) if np.abs(gray(f) - final).mean() < 2.0), None)
    print(f"patchiness mean {np.mean(patch):.3f} max {np.max(patch):.3f}  lag after the storm: {lag} frames (to within 2 levels)")


def grid(out, images):
    ims = [Image.open(p).convert("RGB") for p in images]
    w, h = 320, 180
    cols = 4
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows))
    for i, im in enumerate(ims):
        sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
    sheet.save(out)
    print(f"contact sheet: {out}")


if __name__ == "__main__":
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == "switch":
        switch(rest[0])
    elif cmd == "scroll":
        scroll(rest[0], rest[1])
    elif cmd == "weather":
        weather(rest[0], int(rest[1]) if len(rest) > 1 else 120)
    elif cmd == "grid":
        grid(rest[0], rest[1:])
    else:
        sys.exit(__doc__)
