#!/usr/bin/env python3
"""Earthworm Donkey Country's art and levels, generated: run from the demo's root.

Each level's ground is written once here, as lines (world units, y up): this
script writes it into the level's Tiled map (object layers `ground` and
`platform`) and paints the art over the same lines, so what you see is what
you stand on.
"""
import json
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 720  # level height, world units = pixels

# Level 1, "Jungle Hills": solid ground chains, one-way platforms (y up).
LEVEL1 = {
    "width": 3840,
    "ground": [
        [(0, 720), (0, 160), (480, 160), (760, 260), (900, 280), (1040, 250), (1240, 160), (1700, 160),
         (1700, 184), (2000, 184), (2000, 0)],
        [(2220, 0), (2220, 140), (2600, 140), (2860, 220), (3200, 220), (3500, 170), (3840, 170), (3840, 720)],
    ],
    "platforms": [[(1180, 330), (1360, 330)], [(2400, 300), (2560, 300)]],
    "spawn": (120, 160),
    "goal": (3720, 170),
}


def tiled(level, name):
    """The level as a Tiled map: image layers for the art, object layers for the ground."""
    def polyline(i, pts, cls):
        x0, y0 = pts[0]
        return {"id": i, "name": "", "type": cls, "x": x0, "y": H - y0, "rotation": 0, "visible": True,
                "width": 0, "height": 0, "polyline": [{"x": x - x0, "y": -(y - y0)} for x, y in pts]}

    def point(i, name, p):
        return {"id": i, "name": name, "type": name, "x": p[0], "y": H - p[1], "point": True, "rotation": 0,
                "visible": True, "width": 0, "height": 0}

    def image(i, name, file, parallax, repeat):
        return {"id": i, "name": name, "type": "imagelayer", "image": f"../textures/{file}", "opacity": 1,
                "visible": True, "x": 0, "y": 0, "offsetx": 0, "offsety": 0, "parallaxx": parallax,
                "parallaxy": 1, "repeatx": repeat}

    objects, n = [], 1
    for chain in level["ground"]:
        objects.append(polyline(n, chain, "ground"))
        n += 1
    for chain in level["platforms"]:
        objects.append(polyline(n, chain, "platform"))
        n += 1
    markers = [point(n, "spawn", level["spawn"]), point(n + 1, "goal", level["goal"])]
    return {
        "type": "map", "version": "1.10", "orientation": "orthogonal", "renderorder": "right-down",
        "width": level["width"] // 40, "height": H // 40, "tilewidth": 40, "tileheight": 40, "infinite": False,
        "tilesets": [], "nextobjectid": n + 2, "nextlayerid": 7,
        "layers": [
            image(1, "sky", "sky.png", 0, True),
            image(2, "hills", "hills.png", 0.25, True),
            image(3, "jungle", "jungle.png", 0.55, True),
            image(4, "ground art", f"{name}.png", 1, False),
            {"id": 5, "name": "ground", "type": "objectgroup", "objects": objects, "opacity": 1, "visible": True,
             "x": 0, "y": 0},
            {"id": 6, "name": "markers", "type": "objectgroup", "objects": markers, "opacity": 1, "visible": True,
             "x": 0, "y": 0},
        ],
    }


def gradient(w, h, top, bottom):
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    return img


def ridge(w, h, base, amp, waves, color, seed):
    """A silhouette of rolling hills or treetops, seamless across its width."""
    rnd = random.Random(seed)
    phases = [rnd.uniform(0, math.tau) for _ in waves]
    pts = []
    for x in range(0, w + 1, 4):
        y = base + sum(amp * a * math.sin(math.tau * k * x / w + p) for (k, a), p in zip(waves, phases))
        pts.append((x, h - y))
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(img).polygon(pts + [(w, h), (0, h)], fill=color)
    return img


def painted_ground(level):
    """Dirt under the ground lines, grass along their tops, planks under the platforms."""
    w = level["width"]
    rnd = random.Random(7)
    dirt = Image.new("RGBA", (w, H), (0, 0, 0, 0))
    mask = Image.new("L", (w, H), 0)
    md = ImageDraw.Draw(mask)
    for chain in level["ground"]:  # closed along the level's bottom
        outline = chain + [(chain[-1][0], 0), (chain[0][0], 0)]
        md.polygon([(x, H - y) for x, y in outline], fill=255)
    tex = gradient(w, H, (150, 96, 52), (92, 54, 30))
    td = ImageDraw.Draw(tex)
    for _ in range(w * H // 900):  # pebbles and roots
        x, y, r = rnd.randrange(w), rnd.randrange(H), rnd.randint(2, 6)
        td.ellipse([x - r, y - r, x + r, y + r], fill=(118, 74, 40, 255) if rnd.random() < 0.7 else (176, 132, 84, 255))
    dirt.paste(tex, (0, 0), mask)
    d = ImageDraw.Draw(dirt)
    for chain in level["ground"]:
        for (x0, y0), (x1, y1) in zip(chain, chain[1:]):
            if abs(x1 - x0) < 1 or abs(y1 - y0) > 1.2 * abs(x1 - x0):
                continue  # walls get no grass
            d.line([(x0, H - y0 + 7), (x1, H - y1 + 7)], fill=(46, 120, 40, 255), width=16)
            d.line([(x0, H - y0 + 1), (x1, H - y1 + 1)], fill=(108, 186, 62, 255), width=6)
            for _ in range(int(abs(x1 - x0) / 9)):  # tufts
                t = rnd.random()
                gx, gy = x0 + (x1 - x0) * t, H - (y0 + (y1 - y0) * t)
                d.line([(gx, gy), (gx + rnd.uniform(-4, 4), gy - rnd.uniform(5, 12))], fill=(86, 160, 52, 255), width=2)
    gx, gy = level["goal"]  # the flag at the end
    d.rectangle([gx - 3, H - gy - 150, gx + 3, H - gy], fill=(90, 70, 50, 255))
    d.polygon([(gx + 3, H - gy - 150), (gx + 70, H - gy - 128), (gx + 3, H - gy - 106)], fill=(236, 196, 60, 255),
              outline=(150, 110, 30, 255))
    d.ellipse([gx + 20, H - gy - 136, gx + 36, H - gy - 120], fill=(250, 232, 120, 255))  # a banana-yellow sun on it
    for (x0, y), (x1, _) in level["platforms"]:
        d.rectangle([x0, H - y, x1, H - y + 14], fill=(122, 78, 40, 255), outline=(70, 42, 20, 255), width=2)
        for x in range(int(x0) + 30, int(x1), 30):
            d.line([(x, H - y + 2), (x, H - y + 12)], fill=(90, 56, 28, 255), width=2)
    return dirt


def donkey_frame(phase, pose):
    """The earthworm donkey: a donkey's head on a worm's body. 64x64, facing right."""
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))  # drawn at 2x, then halved: smooth edges
    d = ImageDraw.Draw(img)
    lift = {"jump": -10, "fall": 4}.get(pose, 0)
    segs = 6
    for i in range(segs):  # the worm, tail to head, waving as it runs
        x = 18 + i * 13
        wave = math.sin(phase * math.tau + i * 0.9) * (7 if pose == "run" else 2)
        y = 92 + wave - i * 5 + (lift if i > 2 else 0)
        r = 14 + i * 0.6
        shade = (226 - i * 6, 150 - i * 4, 156 - i * 4, 255)
        d.ellipse([x - r, y - r, x + r, y + r], fill=shade, outline=(150, 84, 92, 255), width=3)
    hx, hy = 96, 50 + lift  # the donkey's head
    d.ellipse([hx - 22, hy - 18, hx + 22, hy + 20], fill=(150, 140, 132, 255), outline=(86, 78, 72, 255), width=3)
    d.ellipse([hx + 4, hy + 2, hx + 30, hy + 24], fill=(190, 182, 172, 255), outline=(86, 78, 72, 255), width=3)  # muzzle
    for ex, tilt in ((hx - 12, -0.5), (hx + 2, 0.15)):  # ears
        tip = (ex + 26 * math.sin(tilt), hy - 46 + (4 if pose == "fall" else 0))
        d.polygon([(ex - 7, hy - 10), (ex + 7, hy - 12), tip], fill=(140, 130, 122, 255), outline=(86, 78, 72, 255))
    d.ellipse([hx - 2, hy - 8, hx + 8, hy + 2], fill=(255, 255, 255, 255))
    d.ellipse([hx + 2, hy - 6, hx + 8, hy], fill=(30, 24, 20, 255))
    d.ellipse([hx + 20, hy + 8, hx + 26, hy + 14], fill=(60, 50, 46, 255))  # nostril
    return img.resize((64, 64), Image.LANCZOS)


def main():
    random.seed(1)
    tex = os.path.join(ROOT, "assets", "textures")
    maps = os.path.join(ROOT, "assets", "maps")
    art = os.path.join(ROOT, "art", "donkey")
    for d in (tex, maps, art):
        os.makedirs(d, exist_ok=True)
    gradient(1280, H, (120, 196, 236), (226, 240, 220)).save(os.path.join(tex, "sky.png"))
    ridge(2048, H, 300, 60, [(1, 1.0), (3, 0.4), (7, 0.15)], (120, 170, 120, 255), 2).save(os.path.join(tex, "hills.png"))
    jungle = ridge(2048, H, 220, 50, [(2, 0.8), (9, 0.35), (23, 0.2)], (40, 104, 56, 255), 3)
    jungle.filter(ImageFilter.GaussianBlur(1)).save(os.path.join(tex, "jungle.png"))
    painted_ground(LEVEL1).save(os.path.join(tex, "level1.png"))
    with open(os.path.join(maps, "level1.tmj"), "w") as f:
        json.dump(tiled(LEVEL1, "level1"), f, indent=1)
    frames = {"idle": [0, 0.5], "run": [0, 0.25, 0.5, 0.75], "jump": [0], "fall": [0]}
    sources = []
    for pose, phases in frames.items():
        for i, phase in enumerate(phases):
            path = os.path.join(art, f"{pose}{i}.png")
            donkey_frame(phase, pose).save(path)
            sources.append(f"art/donkey/{pose}{i}.png")
    os.makedirs(os.path.join(ROOT, "assets", "atlases"), exist_ok=True)
    with open(os.path.join(ROOT, "assets", "atlases", "donkey.atlas.json"), "w") as f:
        json.dump({"sources": sources, "filter": "linear", "padding": 2}, f, indent=2)


if __name__ == "__main__":
    main()
