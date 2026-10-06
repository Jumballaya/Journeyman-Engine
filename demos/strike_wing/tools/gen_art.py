#!/usr/bin/env python3
"""Generates Strike Wing's original pixel art (water, islands, clouds,
bullets, particles, app icon) in the Kenney Pixel Shmup palette.

Re-run after editing:  python3 tools/gen_art.py   (requires Pillow + numpy)
Outputs go to assets/textures/fx/ and assets/icon.png.
"""
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
FX = os.path.join(ROOT, "assets", "textures", "fx")
rng = np.random.default_rng(1942)


def hexc(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


GRASS, GRASS_DK, GRASS_LT = hexc("b6d53c"), hexc("a6c42e"), hexc("c9e750")
SAND, FOAM, OUTLINE = hexc("fee481"), hexc("dff6f5"), hexc("434a5f")
TREE, TREE_LT, DIRT = hexc("345551"), hexc("92b115"), hexc("cb815e")


def save(img, name):
    path = os.path.join(FX, name)
    img.save(path)
    print("wrote", path, img.size)


def smooth_noise(w, h, scale, seed, wrap=True):
    """Value noise, optionally tileable."""
    r = np.random.default_rng(seed)
    gw, gh = max(2, w // scale), max(2, h // scale)
    grid = r.random((gh + 1, gw + 1))
    if wrap:
        grid[-1, :] = grid[0, :]
        grid[:, -1] = grid[:, 0]
    ys = np.linspace(0, gh, h, endpoint=not wrap)
    xs = np.linspace(0, gw, w, endpoint=not wrap)
    y0 = np.floor(ys).astype(int).clip(0, gh - 1)
    x0 = np.floor(xs).astype(int).clip(0, gw - 1)
    fy = (ys - y0)[:, None]
    fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = grid[y0][:, x0]
    b = grid[y0][:, x0 + 1]
    c = grid[y0 + 1][:, x0]
    d = grid[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def water(name, base, mid, light, seed):
    """32x32 tileable water: two-tone body with short wave highlights."""
    w = h = 32
    n = smooth_noise(w, h, 8, seed)
    img = Image.new("RGBA", (w, h), base)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if n[y, x] > 0.62:
                px[x, y] = mid
    r = np.random.default_rng(seed + 1)
    for _ in range(9):  # little wave crests: 3-4 px horizontal dashes
        x, y = int(r.integers(0, w)), int(r.integers(0, h))
        for k in range(int(r.integers(2, 5))):
            px[(x + k) % w, y] = light
            if k == 1:
                px[(x + k) % w, (y - 1) % h] = light
    save(img, name)


def island(name, w, h, seed, trees):
    """Blob island: foam ring, sand beach, grass with darker speckle, trees."""
    yy, xx = np.mgrid[0:h, 0:w]
    cx, cy = (w - 1) / 2, (h - 1) / 2
    d = np.sqrt(((xx - cx) / (w / 2 - 3)) ** 2 + ((yy - cy) / (h / 2 - 3)) ** 2)
    n = smooth_noise(w, h, 6, seed, wrap=False)
    field = d + (n - 0.5) * 0.55
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            f = field[y, x]
            if f < 0.62:
                c = GRASS
                s = n[y, x]
                if s > 0.68:
                    c = GRASS_LT
                elif (x * 7 + y * 13 + seed) % 11 == 0:
                    c = GRASS_DK
                px[x, y] = c
            elif f < 0.78:
                px[x, y] = SAND
            elif f < 0.9:
                px[x, y] = FOAM
    r = np.random.default_rng(seed + 7)
    placed = 0
    for _ in range(200):
        if placed >= trees:
            break
        x, y = int(r.integers(3, w - 4)), int(r.integers(3, h - 4))
        if field[y, x] > 0.45:
            continue
        # 5x5 round tree: dark outline + light highlight, like tile_0048.
        shape = ["..T..", ".TLT.", "TLLLT", ".TTT.", "..D.."]
        for j, row in enumerate(shape):
            for i, ch in enumerate(row):
                if ch == ".":
                    continue
                c = {"T": TREE, "L": TREE_LT, "D": DIRT}[ch]
                px[x + i - 2, y + j - 2] = c
        placed += 1
    save(img, name)


def cloud(name, w, h, seed):
    """Soft white cloud with a light blue-grey underside, 2 alpha levels."""
    yy, xx = np.mgrid[0:h, 0:w]
    field = np.zeros((h, w))
    r = np.random.default_rng(seed)
    for _ in range(7):
        bx, by = r.uniform(w * 0.2, w * 0.8), r.uniform(h * 0.35, h * 0.7)
        rad = r.uniform(h * 0.22, h * 0.42)
        field = np.maximum(field, 1 - np.sqrt((xx - bx) ** 2 + (yy - by) ** 2) / rad)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            f = field[y, x]
            if f > 0.0:
                under = y > h * 0.62 and f < 0.45
                c = (205, 222, 235, 255) if under else (255, 255, 255, 255)
                px[x, y] = c if f > 0.18 else (c[0], c[1], c[2], 150)
    save(img, name)


def orb(name, size, core, glow):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    c = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c, y - c) / (size / 2)
            if d < 0.45:
                px[x, y] = (255, 255, 255, 255)
            elif d < 0.75:
                px[x, y] = core
            elif d < 1.0:
                px[x, y] = glow
    save(img, name)


def spark(name):
    img = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
    px = img.load()
    for (x, y) in [(1, 0), (2, 0), (0, 1), (1, 1), (2, 1), (3, 1), (0, 2), (1, 2), (2, 2), (3, 2), (1, 3), (2, 3)]:
        px[x, y] = (255, 255, 255, 255)
    save(img, name)


def icon():
    ship = Image.open(os.path.join(ROOT, "assets", "textures", "shmup", "ships", "ship_0000.png")).convert("RGBA")
    size = 512
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bpx = bg.load()
    for y in range(size):
        for x in range(size):
            # Rounded square in deep sea blue with a lighter top.
            rx, ry = abs(x - 255.5) - 200, abs(y - 255.5) - 200
            inside = (max(rx, 0) ** 2 + max(ry, 0) ** 2) <= 48 ** 2
            if inside:
                t = y / size
                bpx[x, y] = (int(40 + 30 * (1 - t)), int(110 + 40 * (1 - t)), int(190 + 30 * (1 - t)), 255)
    img.alpha_composite(bg)
    big = ship.resize((352, 352), Image.NEAREST)
    img.alpha_composite(big, (80, 70))
    path = os.path.join(ROOT, "assets", "icon.png")
    img.save(path)
    print("wrote", path)


if __name__ == "__main__":
    os.makedirs(FX, exist_ok=True)
    water("water.png", hexc("2a6fb0"), hexc("2f78bb"), hexc("6fb7e8"), 3)
    water("water_storm.png", hexc("1d3b5c"), hexc("22446a"), hexc("5a7fa3"), 5)
    island("island_0.png", 56, 44, 11, 5)
    island("island_1.png", 40, 32, 23, 2)
    island("island_2.png", 72, 52, 37, 8)
    island("island_3.png", 30, 26, 41, 1)
    cloud("cloud_0.png", 64, 32, 1)
    cloud("cloud_1.png", 48, 24, 2)
    cloud("cloud_2.png", 80, 36, 3)
    orb("orb.png", 8, (255, 90, 120, 255), (255, 60, 90, 140))
    orb("orb_big.png", 12, (255, 170, 60, 255), (255, 120, 40, 140))
    orb("orb_blue.png", 8, (90, 200, 255, 255), (60, 140, 255, 140))
    spark("spark.png")
    icon()
