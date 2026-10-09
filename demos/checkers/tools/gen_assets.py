#!/usr/bin/env python3
"""Checkers' art: a piece and a crown (light gray: sprites tint them).
Re-run after editing: python3 tools/gen_assets.py  (needs Pillow)."""
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


def save(img, name):
    os.makedirs(ROOT, exist_ok=True)
    img.save(os.path.join(ROOT, name))


def piece():
    """A round piece with a rim and a ring, lit from the top left."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([1, 3, 30, 31], fill=(70, 70, 70, 255))      # side, seen below
    d.ellipse([1, 1, 30, 28], fill=(235, 235, 235, 255))   # top
    d.ellipse([7, 6, 24, 23], outline=(185, 185, 185, 255), width=2)
    d.ellipse([5, 4, 12, 9], fill=(255, 255, 255, 255))    # highlight
    save(img, "piece.png")


def crown():
    img = Image.new("RGBA", (16, 12), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.polygon([(1, 11), (1, 3), (5, 7), (8, 1), (11, 7), (15, 3), (15, 11)], fill=(255, 255, 255, 255))
    save(img, "crown.png")


if __name__ == "__main__":
    piece()
    crown()
