#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Makes src/icons/caustics.png: the light that moves over the background,
# as at the bottom of a pool. A network of soft bright lines (the edges of a
# Voronoi pattern, F2 - F1, its plane bent by slow waves so the lines curve)
# that tiles seamlessly, white with the light in its alpha, so the window
# can slide two copies of it across each other.
#
#   python3 scripts/make-caustics.py [out.png]
import sys

import numpy as np
from PIL import Image, ImageFilter

SIZE = 512
GRID = 6
SEED = 7


def caustics(size=SIZE, grid=GRID, seed=SEED):
    rng = np.random.default_rng(seed)
    # One point in each square of a grid, never on its border: the cells
    # come out even, with no two points crowding each other.
    step = size / grid
    points = np.array([[(i + 0.2 + 0.6 * rng.random()) * step, (j + 0.2 + 0.6 * rng.random()) * step]
                       for i in range(grid) for j in range(grid)])
    # Every point and its copies around the tile: distances wrap, so the
    # pattern has no seams.
    shifted = np.concatenate([points + np.array([dx, dy]) * size
                              for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    # Slow waves bend the plane; whole periods across the tile keep it seamless.
    tau = 2 * np.pi / size
    wx = xs + 14 * np.sin(tau * (2 * ys) + 1.3) + 7 * np.sin(tau * (3 * xs + 2 * ys) + 0.4)
    wy = ys + 14 * np.sin(tau * (2 * xs) + 2.1) + 7 * np.sin(tau * (2 * xs - 3 * ys) + 1.7)
    xs, ys = wx, wy
    first = np.full((size, size), np.inf, dtype=np.float32)
    second = np.full((size, size), np.inf, dtype=np.float32)
    for px, py in shifted:
        d = np.hypot(xs - px, ys - py)
        second = np.where(d < first, first, np.minimum(second, d))
        first = np.minimum(first, d)
    edge = second - first
    # Thin bright lines, fading softly away from the edges.
    light = np.exp(-edge / 4.0) ** 1.4 + 0.22 * np.exp(-edge / 16.0)
    light = light / light.max()
    alpha = Image.fromarray((light * 255).astype(np.uint8), "L")
    # Wrap before blurring so the soft edges still meet across the seam.
    tiled = Image.new("L", (size * 3, size * 3))
    for i in range(3):
        for j in range(3):
            tiled.paste(alpha, (i * size, j * size))
    tiled = tiled.filter(ImageFilter.GaussianBlur(2.2))
    alpha = tiled.crop((size, size, size * 2, size * 2))
    out = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "src/icons/caustics.png"
    caustics().save(target, optimize=True)
