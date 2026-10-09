#!/usr/bin/env python3
"""Tank Arena's art: a tank seen from above (light gray: sprites tint it per
player), a shell, a crate. Re-run after editing: python3 tools/gen_assets.py
(needs Pillow)."""
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


def save(img, name):
    os.makedirs(ROOT, exist_ok=True)
    img.save(os.path.join(ROOT, name))


def tank():
    """Treads, a hull and a turret, the barrel pointing up."""
    img = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([2, 4, 6, 21], fill=(70, 70, 76, 255))      # left tread
    d.rectangle([17, 4, 21, 21], fill=(70, 70, 76, 255))    # right tread
    for y in range(5, 21, 3):                                # tread links
        d.line([2, y, 6, y], fill=(40, 40, 46, 255))
        d.line([17, y, 21, y], fill=(40, 40, 46, 255))
    d.rectangle([6, 6, 17, 20], fill=(225, 225, 225, 255))  # hull
    d.rectangle([6, 18, 17, 20], fill=(170, 170, 170, 255))
    d.ellipse([8, 9, 15, 16], fill=(190, 190, 190, 255))    # turret
    d.rectangle([11, 0, 12, 11], fill=(120, 120, 126, 255))  # barrel
    save(img, "tank.png")


def shell():
    img = Image.new("RGBA", (6, 6), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([0, 0, 5, 5], fill=(255, 240, 200, 255))
    d.ellipse([1, 1, 3, 3], fill=(255, 255, 255, 255))
    save(img, "shell.png")


def crate():
    img = Image.new("RGBA", (16, 16), (120, 84, 50, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 15, 15], outline=(70, 48, 28, 255))
    d.line([1, 1, 14, 14], fill=(150, 108, 66, 255), width=2)
    d.line([1, 14, 14, 1], fill=(150, 108, 66, 255), width=2)
    save(img, "crate.png")


if __name__ == "__main__":
    tank()
    shell()
    crate()
