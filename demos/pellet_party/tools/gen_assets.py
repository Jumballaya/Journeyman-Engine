#!/usr/bin/env python3
"""Pellet Party's art: a blob (tinted per player), a pellet.
Re-run after editing: python3 tools/gen_assets.py  (needs Pillow)."""
import math
import os

from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


def save(img, name):
    os.makedirs(ROOT, exist_ok=True)
    img.save(os.path.join(ROOT, name))


def blob():
    """A soft round body, light gray so sprites tint it, with eyes."""
    n = 24
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    c = (n - 1) / 2
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - c, (y - c) * 1.08)
            if d > 11.2:
                continue
            shade = 255 - int(max(0, (x - c) + (y - c)) * 5)  # darker toward the bottom right
            if d > 10.0:
                shade = int(shade * 0.55)  # rim
            img.putpixel((x, y), (shade, shade, shade, 255))
    for ex in (8, 15):  # eyes
        for y in range(8, 12):
            img.putpixel((ex, y), (20, 20, 30, 255))
            img.putpixel((ex + 1, y), (20, 20, 30, 255))
        img.putpixel((ex, 8), (255, 255, 255, 255))
    save(img, "blob.png")


def pellet():
    """A small gem."""
    n = 10
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    c = (n - 1) / 2
    for y in range(n):
        for x in range(n):
            d = abs(x - c) + abs(y - c)
            if d > 4.6:
                continue
            v = 255 if x < c and y < c else 210 if x < c or y < c else 160
            img.putpixel((x, y), (v, v, v, 255))
    save(img, "pellet.png")


if __name__ == "__main__":
    blob()
    pellet()
