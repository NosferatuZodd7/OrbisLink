#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Makes src/icons/room-dark.jpg and room-light.jpg (needs Pillow and numpy):
#   python3 scripts/make-room.py src/icons
#
# A soft, out-of-focus room for the glass to float in: drawn from simple
# shapes, then blurred heavily. Two moods: a warm evening (dark) and a bright
# daytime interior (light).
import sys, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
out = sys.argv[1]
W, H = 1920, 1200

def room(mood):
    random.seed(7)
    dark = mood == "dark"
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    top, bottom = ((44, 34, 28), (18, 14, 12)) if dark else ((222, 218, 212), (186, 180, 172))
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    # Back wall, a lighter block in the middle distance.
    d.rectangle([W * 0.18, H * 0.10, W * 0.78, H * 0.70], fill=(70, 54, 44) if dark else (236, 233, 228))
    # Wooden slats on the right.
    for i in range(16):
        x = W * 0.80 + i * 26
        d.rectangle([x, H * 0.12, x + 13, H * 0.72], fill=(96, 62, 38) if dark else (196, 182, 166))
    # A tall window of daylight on the left.
    d.rectangle([W * 0.02, H * 0.05, W * 0.16, H * 0.62], fill=(232, 205, 160) if dark else (250, 250, 248))
    # Ceiling light strips.
    for x in (0.25, 0.48, 0.70):
        d.rectangle([W * x, H * 0.03, W * (x + 0.14), H * 0.045], fill=(255, 236, 200) if dark else (255, 255, 255))
    # Floor.
    d.rectangle([0, H * 0.72, W, H], fill=(58, 44, 36) if dark else (204, 200, 194))
    # A plant (dark mass) at the left, a sofa (pale) at the right, a lamp.
    d.ellipse([W * -0.04, H * 0.30, W * 0.12, H * 0.90], fill=(24, 30, 20) if dark else (92, 104, 84))
    d.rounded_rectangle([W * 0.70, H * 0.62, W * 1.02, H * 0.92], 60, fill=(196, 176, 150) if dark else (244, 242, 238))
    d.ellipse([W * 0.86, H * 0.40, W * 0.94, H * 0.52], fill=(255, 190, 110) if dark else (250, 230, 200))
    # A low table with warm reflections.
    d.ellipse([W * 0.38, H * 0.78, W * 0.60, H * 0.88], fill=(120, 86, 56) if dark else (172, 160, 146))
    img = img.filter(ImageFilter.GaussianBlur(46)).filter(ImageFilter.GaussianBlur(28))
    # A little grain, so the gradients do not band.
    a = np.asarray(img).astype(np.float32)
    a += np.random.default_rng(3).normal(0, 1.6, a.shape)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

for mood in ("dark", "light"):
    room(mood).save(f"{out}/room-{mood}.jpg", quality=86, optimize=True)
