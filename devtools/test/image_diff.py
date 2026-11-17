"""Compare two screenshots: max channel difference, mean difference and the
share of pixels that differ by more than a threshold (0-255 scale).

    python devtools/test/image_diff.py a.png b.png [--threshold 8]

Exit code 1 if the images have different sizes or more than --max-share
percent of pixels differ by more than the threshold.
"""

import argparse
import sys

from PIL import Image, ImageChops, ImageStat


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--threshold", type=int, default=8)
    parser.add_argument("--max-share", type=float, default=0.1, help="percent of pixels allowed above the threshold")
    args = parser.parse_args()

    a = Image.open(args.a).convert("RGB")
    b = Image.open(args.b).convert("RGB")
    if a.size != b.size:
        print(f"IMAGE_DIFF: size {a.size} != {b.size}")
        return 1

    diff = ImageChops.difference(a, b)
    r, g, bl = diff.split()
    pixel_max = ImageChops.lighter(ImageChops.lighter(r, g), bl)  # Largest channel difference per pixel.
    histogram = pixel_max.histogram()
    pixels = a.size[0] * a.size[1]
    over = sum(histogram[args.threshold + 1 :])
    share = 100.0 * over / pixels
    mean = sum(ImageStat.Stat(diff).mean) / 3.0
    print(f"IMAGE_DIFF: max={pixel_max.getextrema()[1]} mean={mean:.3f} over{args.threshold}={share:.3f}%")
    return 0 if share <= args.max_share else 1


if __name__ == "__main__":
    sys.exit(main())
