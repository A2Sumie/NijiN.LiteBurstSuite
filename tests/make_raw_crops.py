#!/usr/bin/env python3
"""Create small CFA-preserving DNG regression inputs from real RAW exposures."""
import argparse
from pathlib import Path

import numpy as np
import rawpy
import tifffile


def crop(source: Path, destination: Path, size: int) -> None:
    with rawpy.imread(str(source)) as raw:
        image = raw.raw_image_visible
        y = max(0, (image.shape[0] - size) // 12 * 6)
        x = max(0, (image.shape[1] - size) // 12 * 6)
        data = image[y:y + size, x:x + size].copy()
        pattern = raw.raw_colors_visible[y:y + 6, x:x + 6].copy()
        pattern[pattern == 3] = 1
        sony = source.suffix.lower() == ".arw"
        if sony:
            pattern = pattern[:2, :2].copy()
        period = pattern.shape[0]
        levels = raw.black_level_per_channel
        black = [int(levels[int(c)]) for c in pattern.ravel()]
        wb = np.asarray(raw.camera_whitebalance[:3], dtype=float)
        wb = np.where(wb > 0, wb, 1)
        neutral = wb[1] / wb
        matrix = np.asarray(raw.rgb_xyz_matrix[:3], dtype=float)
        make, model = ("Sony", "ILCE-9M3") if sony else ("Fujifilm", "X-H2S")
        if "xh2_40" in source.parent.name:
            model = "X-H2"
        tags = [
            (271, "s", 0, make, False), (272, "s", 0, model, False),
            (33421, "H", 2, (period, period), False),
            (33422, "B", period * period, pattern.tobytes(), False),
            (50706, "B", 4, (1, 4, 0, 0), False),
            (50707, "B", 4, (1, 3, 0, 0), False),
            (50708, "s", 0, f"{make} {model}", False),
            (50710, "B", 3, (0, 1, 2), False),
            (50711, "H", 1, 1, False),
            (50713, "H", 2, (period, period), False),
            (50714, "2I", period * period, tuple(v for b in black for v in (b, 1)), False),
            (50717, "I", 1, int(raw.white_level), False),
            (50721, "2i", 9, tuple(v for m in matrix.ravel() for v in (round(m * 1000000), 1000000)), False),
            (50728, "2I", 3, tuple(v for n in neutral for v in (round(n * 1000000), 1000000)), False),
            (50778, "H", 1, 21, False),
        ]
    destination.parent.mkdir(parents=True, exist_ok=True)
    tifffile.imwrite(destination, data, photometric=32803, metadata=None, extratags=tags)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--size", type=int, default=384)
    args = parser.parse_args()
    if args.size < 96 or args.size % 6:
        parser.error("size must be a multiple of six and at least 96")
    files = sorted(p for p in args.source.iterdir() if p.suffix.lower() in {".arw", ".raf", ".nef"})
    for index, source in enumerate(files[:args.count]):
        target = args.destination / f"frame_{index:03d}.dng"
        crop(source, target, args.size)
        print(source.name, "->", target)


if __name__ == "__main__":
    main()
