#!/usr/bin/env python3
"""The hub's button pictures (hub/buttons/<name>.png) from the PS5 button
icons (the white "full solid" set's SVGs).

Each icon is drawn on a square canvas with its drawing moved to the exact
middle (the set's icons sit a few pixels off in their squares), at the set's
own scale, so the hub can draw any of them centred on a point and they keep
their sizes next to each other.

    python3 hub_buttons.py <icons' SVG folder> <out folder> [--size 128]
"""
import argparse
import io
import os

import cairosvg
import numpy as np
from PIL import Image

# the hub's names -> the set's files
ICONS = {"Cross": "Cross", "Circle": "Circle", "Square": "Square", "Triangle": "Triangle", "L1": "L1", "R1": "R1",
         "L2": "L2", "R2": "R2", "Options": "Options", "Create": "Create", "D-Pad": "D-Pad",
         "R3": "Right Stick Click", "L3": "Left Stick Click", "Touch Pad": "Touch Pad Press"}
WORK = 1024   # drawn this big to place them, then made smaller


def centred(svg, size):
    png = cairosvg.svg2png(url=svg, output_width=WORK, output_height=WORK)
    a = np.asarray(Image.open(io.BytesIO(png)).convert("RGBA")).copy()
    ys, xs = np.nonzero(a[..., 3] > 0)
    dx = round(WORK / 2 - (xs.min() + xs.max() + 1) / 2)
    dy = round(WORK / 2 - (ys.min() + ys.max() + 1) / 2)
    out = np.zeros_like(a)
    h, w = a.shape[:2]
    out[max(0, dy):h + min(0, dy), max(0, dx):w + min(0, dx)] = a[max(0, -dy):h - max(0, dy), max(0, -dx):w - max(0, dx)]
    # smaller, in premultiplied alpha (clean edges)
    f = out.astype(np.float32) / 255
    pm = np.dstack([f[..., :3] * f[..., 3:], f[..., 3:]])
    small = np.dstack([np.asarray(Image.fromarray(pm[..., i]).resize((size, size), Image.LANCZOS)) for i in range(4)])
    al = np.clip(small[..., 3:], 0, 1)
    rgb = np.where(al > 1e-3, small[..., :3] / np.maximum(al, 1e-3), 1)
    return Image.fromarray((np.dstack([np.clip(rgb, 0, 1), al]) * 255).round().astype(np.uint8), "RGBA")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("svg_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--size", type=int, default=128)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    for name, file in ICONS.items():
        svg = os.path.join(a.svg_dir, file + ".svg")
        if os.path.exists(svg):
            centred(svg, a.size).save(os.path.join(a.out_dir, name + ".png"))
