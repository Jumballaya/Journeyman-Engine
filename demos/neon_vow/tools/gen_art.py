#!/usr/bin/env python3
"""Neon Vow's art and levels, generated: run from the demo's root.

Each level's ground is written once here, as curves (world units, y up): this
script writes it into the level's Tiled map (object layers `ground` and
`platform`) and paints the art over the same lines, so what you see is what
you stand on.
"""
import json
import math
import os
import random
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 720  # level height, world units = pixels

def bezier(a, b, c, d, spacing=16):
    """Sample a cubic with segments no longer than spacing world units."""
    length = sum(math.dist(p, q) for p, q in zip((a, b, c), (b, c, d)))
    count = max(1, math.ceil(length / spacing))
    return [tuple(round((1-t)**3*a[j] + 3*(1-t)**2*t*b[j] +
                        3*(1-t)*t*t*c[j] + t**3*d[j], 4) for j in (0, 1))
            for t in (i / count for i in range(count + 1))]


def profile(knots, spacing=16):
    """Smooth height profile, with level tangents at crests and swales; no overshoot."""
    secants = [(b[1]-a[1]) / (b[0]-a[0]) for a, b in zip(knots, knots[1:])]
    tangents = [0]
    for a, b in zip(secants, secants[1:]):
        tangents.append(0 if a*b <= 0 else 2*a*b/(a+b))
    tangents.append(0)
    result = [knots[0]]
    for i, (a, d) in enumerate(zip(knots, knots[1:])):
        dx = (d[0]-a[0]) / 3
        result += bezier(a, (a[0]+dx, a[1]+dx*tangents[i]),
                         (d[0]-dx, d[1]-dx*tangents[i+1]), d, spacing)[1:]
    return result


# Walls keep their steep faces; their rounded feet and lips join the walking curves.
LEFT_BANK = profile([(0, 160), (120, 160), (280, 160), (350, 153), (460, 160),
                     (620, 204), (800, 270), (900, 280), (1020, 249), (1160, 190),
                     (1280, 163), (1410, 154), (1540, 164), (1630, 156), (1676, 158)])
STEP = (bezier((1676, 158), (1682, 158), (1687, 159), (1688, 163), 5) +
        bezier((1688, 163), (1690, 174), (1689, 194), (1694, 203), 8)[1:] +
        bezier((1694, 203), (1698, 210), (1706, 211), (1716, 211), 5)[1:])
CHECKPOINT_BANK = profile([(1716, 211), (1800, 207), (1880, 200), (1940, 194), (2000, 184)])
LEFT_CLIFF = bezier((2000, 184), (2016, 139), (1992, 85), (2008, 0), 14)
RIGHT_CLIFF = bezier((2410, 0), (2390, 63), (2406, 123), (2420, 144), 14)
RIGHT_BANK = profile([(2420, 144), (2480, 151), (2570, 145), (2690, 190),
                      (2830, 223), (3000, 230), (3200, 218), (3350, 193),
                      (3470, 177), (3600, 170), (3720, 174), (3840, 171), (3940, 170)])
POD_CLIFF = bezier((3940, 170), (3958, 138), (3932, 55), (3948, 0), 14)
LANDING_CLIFF = bezier((4808, 0), (4796, 78), (4814, 146), (4830, 175), 14)
LANDING_BANK = profile([(4830, 175), (4960, 189), (5140, 178), (5320, 184), (5440, 180)])


def ground_height(x):
    for chain in (LEFT_BANK, STEP, CHECKPOINT_BANK, RIGHT_BANK, LANDING_BANK):
        for (x0, y0), (x1, y1) in zip(chain, chain[1:]):
            if x0 <= x <= x1:
                return y0 + (y1-y0) * (x-x0) / (x1-x0)
    raise ValueError(f"No walking ground at x={x}")


def ground_point(x):
    return (x, round(ground_height(x), 4))


# Level 1, "The Silent Ward": solid ground chains, one-way platforms (y up).
LEVEL1 = {
    "width": 5440,
    "ground": [
        [(0, 720)] + LEFT_BANK + STEP[1:] + CHECKPOINT_BANK[1:] + LEFT_CLIFF[1:],
        RIGHT_CLIFF + RIGHT_BANK[1:] + POD_CLIFF[1:],
        LANDING_CLIFF + LANDING_BANK[1:] + [(5440, 720)],
    ],
    "platforms": [profile([(1180, 310), (1275, 302), (1370, 310)]),
                  profile([(2660, 310), (2750, 318), (2840, 310)])],
    "cables": [(2130, 660, 320)],  # anchor x, y, rope length: over the pit too wide to jump
    "sentries": [ground_point(x) for x in (380, 1600, 3020)],
    "checkpoints": [ground_point(1880), ground_point(3760)],  # passing one saves your place
    "spawn": ground_point(120),
    "goal": ground_point(5320),
    "pods": [(3890, 195, 44, False, 1250), (4220, 370, 30, True, 1250),
             (4550, 455, 20, False, 1050)],
    "shards": [(x, ground_height(x)+30) for x in (220, 540, 780, 1000, 1450, 1810, 1910,
                                                2520, 2890, 3260, 3580, 3820, 4960, 5240)] +
              [(1275, 333), (2750, 349), (4040, 310), (4370, 431), (4690, 483), (4840, 462)],
}


def tiled(level, name):
    """The level as a Tiled map: image layers for the art, object layers for the ground."""
    def polyline(i, pts, cls):
        x0, y0 = pts[0]
        return {"id": i, "name": "", "type": cls, "x": x0, "y": H - y0, "rotation": 0, "visible": True,
                "width": 0, "height": 0, "polyline": [{"x": x - x0, "y": -(y - y0)} for x, y in pts]}

    def point(i, name, p, **props):
        o = {"id": i, "name": name, "type": name, "x": p[0], "y": H - p[1], "point": True, "rotation": 0,
             "visible": True, "width": 0, "height": 0}
        if props:
            o["properties"] = [{"name": k, "type": "bool" if isinstance(v, bool) else "float", "value": v} for k, v in props.items()]
        return o

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
    markers = []
    def mark(name, p, **props):
        markers.append(point(n + len(markers), name, p, **props))
    mark("spawn", level["spawn"])
    mark("goal", level["goal"])
    for x, y, length in level["cables"]:
        mark("cable", (x, y), length=length)
    for p in level["sentries"]:
        mark("sentry", p)
    for p in level["checkpoints"]:
        mark("checkpoint", p)
    for x, y, angle, rotate, speed in level["pods"]:
        mark("pod", (x, y), angle=angle, rotate=rotate, speed=speed)
    for p in level["shards"]:
        mark("shard", p)
    n += len(markers)
    return {
        "type": "map", "version": "1.10", "orientation": "orthogonal", "renderorder": "right-down",
        "width": level["width"] // 40, "height": H // 40, "tilewidth": 40, "tileheight": 40, "infinite": False,
        "tilesets": [], "nextobjectid": n, "nextlayerid": 7,
        "layers": [
            image(1, "sky", "sky.png", 0, True),
            image(2, "skyline", "skyline.png", 0.25, True),
            image(3, "ruins", "ruins.png", 0.55, True),
            image(4, "ground art", f"{name}.png", 1, False),
            {"id": 5, "name": "ground", "type": "objectgroup", "objects": objects, "opacity": 1, "visible": True,
             "x": 0, "y": 0},
            {"id": 6, "name": "markers", "type": "objectgroup", "objects": markers, "opacity": 1, "visible": True,
             "x": 0, "y": 0},
        ],
    }


# All shapes are painted large; only finished layers are downsampled.
class Paint:
    def __init__(self, w, h, scale=2):
        self.scale = scale
        self.image = Image.new("RGBA", (w * scale, h * scale))
        self.draw = ImageDraw.Draw(self.image)

    def points(self, pts):
        return [(round(x * self.scale), round(y * self.scale)) for x, y in pts]

    def poly(self, pts, color):
        self.draw.polygon(self.points(pts), fill=color)

    def line(self, pts, color, width=1):
        self.draw.line(self.points(pts), fill=color, width=max(1, round(width * self.scale)), joint="curve")

    def box(self, box, color):
        self.draw.rectangle(tuple(round(v * self.scale) for v in box), fill=color)

    def oval(self, box, color):
        self.draw.ellipse(tuple(round(v * self.scale) for v in box), fill=color)

    def finish(self):
        w, h = self.image.size
        return self.image.resize((w // self.scale, h // self.scale), Image.Resampling.LANCZOS)


def gradient(w, h, stops, scale=2):
    p = Paint(w, h, scale)
    for y in range(h * scale):
        t = y / (h * scale - 1)
        for (a, ca), (b, cb) in zip(stops, stops[1:]):
            if a <= t <= b:
                u = (t - a) / (b - a)
                color = tuple(round(x + (z - x) * u) for x, z in zip(ca, cb))
                p.draw.line([(0, y), (w * scale, y)], fill=color)
                break
    return p


def glow(p, marks, radius=10):
    light = Paint(p.image.width // p.scale, p.image.height // p.scale, p.scale)
    for pts, color, width in marks:
        light.line(pts, color, width)
    p.image.alpha_composite(light.image.filter(ImageFilter.GaussianBlur(radius * p.scale)))
    p.draw = ImageDraw.Draw(p.image)


def grain(p, seed, count, colors, span=12):
    rnd = random.Random(seed)
    w, h = p.image.width / p.scale, p.image.height / p.scale
    strokes = Paint(int(w), int(h), p.scale)
    for _ in range(count):
        x, y = rnd.uniform(0, w), rnd.uniform(0, h)
        strokes.line([(x, y), (x + rnd.uniform(1, span), y + rnd.uniform(-2, 2))], rnd.choice(colors), 0.45)
    p.image.alpha_composite(strokes.image)


def sky():
    p = gradient(2048, H, [(0, (13, 19, 40, 255)), (0.43, (63, 57, 78, 255)),
                           (0.72, (150, 98, 111, 255)), (1, (39, 90, 99, 255))])
    rnd = random.Random(11)
    clouds = Paint(2048, H)
    for _ in range(110):
        x, y = rnd.uniform(-250, 2048), rnd.uniform(40, 390)
        width = rnd.uniform(90, 450)
        clouds.oval([x, y, x + width, y + rnd.uniform(6, 30)], rnd.choice([
            (91, 83, 108, 30), (18, 30, 52, 60), (227, 151, 142, 18)]))
    p.image.alpha_composite(clouds.image.filter(ImageFilter.GaussianBlur(12)))
    glow(p, [([(1060, 199), (1060, 202)], (255, 169, 136, 140), 128)], 36)
    p.oval([1008, 148, 1112, 252], (225, 177, 153, 255))
    p.oval([1014, 142, 1115, 241], (64, 59, 80, 255))
    for _ in range(160):
        x, y = rnd.randrange(2048), rnd.randrange(290)
        p.oval([x, y, x + 0.7, y + 0.7], (155, 166, 189, 135))
    grain(p, 3, 9500, [(97, 99, 125, 14), (227, 160, 151, 10)], 22)
    return p.finish()


def tower(p, x, top, width, base, near, rnd):
    shadow = (18, 38, 51, 255) if near else (59, 65, 86, 255)
    face = (26, 49, 60, 255) if near else (70, 74, 94, 255)
    edge = (66, 102, 109, 255) if near else (100, 103, 121, 255)
    plate(p, [(x, base), (x, top + 16), (x + width * .18, top + 16), (x + width * .24, top),
              (x + width * .8, top), (x + width, top + 24), (x + width, base)], shadow, shadow)
    plate(p, [(x + width * .65, top + 3), (x + width * .8, top), (x + width, top + 24),
              (x + width, base), (x + width * .65, base)], face, edge)
    p.line([(x + 2, top + 18), (x + width * .2, top + 18), (x + width * .25, top + 2)], edge, 1)
    for y in range(int(top) + 38, int(base), 20 if near else 15):
        p.line([(x + 4, y), (x + width - 5, y)], face, 2)
        for wx in range(int(x) + 8, int(x + width) - 8, 13):
            if rnd.random() < .32:
                p.box([wx, y + 3, wx + 2, y + rnd.randrange(7, 13)],
                      rnd.choice([(141, 126, 126, 220), (94, 150, 155, 210), (197, 141, 126, 200)]))
    if near:
        p.line([(x + width * .65, top + 6), (x + width * .65, base)], edge, .8)
        for _ in range(5):
            y = rnd.uniform(top + 30, base)
            p.line([(x + width * .1, y), (x + width * .28, y + 12), (x + width * .2, y + 30)], shadow, 2)
    p.line([(x + width * .3, top), (x + width * .3, top - rnd.uniform(8, 35))], edge, 1)


def city(near=False):
    p = Paint(2048, H)
    rnd = random.Random(31 if near else 23)
    if near:
        for x in range(-160, 2048, 340):
            tower(p, x, rnd.randint(220, 340), rnd.randint(90, 180), H, True, rnd)
            p.poly([(x - 50, 435), (x + 320, 421), (x + 320, 439), (x - 50, 454)], (25, 46, 59, 255))
            p.line([(x - 50, 434), (x + 320, 420)], (98, 134, 137, 255), 1)
            for bx in range(x, x + 310, 40):
                p.line([(bx, 452), (bx + 30, 437)], (36, 66, 74, 255), 2)
            # Hanging conduits and a broken illuminated sign.
            p.line(catenary((x + 80, 330), (x + 320, 298), 52), (36, 57, 68, 255), 2)
            p.box([x + 40, 300, x + 56, 352], (34, 40, 57, 255))
            glow(p, [([(x + 47, 306), (x + 47, 340)], (240, 111, 112, 95), 4)], 7)
            for sy in (307, 321, 335):
                p.line([(x + 43, sy), (x + 51, sy + 2), (x + 45, sy + 8)], (210, 139, 138, 255), 1.2)
            foliage(p, x + 60, 435, 180, 48, rnd, distant=True)
            foliage(p, x + 225, 512, 220, 83, rnd, distant=True)
            for j in range(6):
                vx = x + j*35
                p.line(bezier((vx, 439), (vx-22, 471), (vx+14, 510), (vx-10, 560), 5),
                       (40, 70, 69, 255), 2)
    else:
        x = -40
        while x < 2048:
            width = rnd.randint(40, 110)
            tower(p, x, rnd.randint(250, 455), width, H, False, rnd)
            x += width + rnd.randint(5, 24)
        p.line([(0, 452), (2048, 420)], (96, 94, 114, 255), 5)
    haze = gradient(2048, H, [(0, (58, 94, 109, 0)), (.55, (64, 98, 108, 0)),
                            (1, (72, 112, 120, 200 if near else 235))])
    p.image.alpha_composite(haze.image)
    mist = Paint(2048, H)
    for _ in range(24):
        x, y = rnd.uniform(-200, 2048), rnd.uniform(420, 700)
        mist.oval([x, y, x + rnd.uniform(180, 550), y + rnd.uniform(12, 70)],
                  (139, 174, 175, 13 if near else 20))
    p.image.alpha_composite(mist.image.filter(ImageFilter.GaussianBlur(24)))
    p.image = p.image.filter(ImageFilter.GaussianBlur(1.0 if near else 1.6))
    return p.finish()


def catenary(a, b, sag, spacing=6):
    count = max(2, math.ceil(math.dist(a, b) / spacing))
    return [(a[0] + (b[0]-a[0])*t,
             a[1] + (b[1]-a[1])*t + sag*(math.cosh(1)-math.cosh(2*t-1))/(math.cosh(1)-1))
            for t in (i/count for i in range(count+1))]


def rock_outline(x, y, rx, ry, phase):
    return [(x + rx*math.cos(t)*r, y + ry*math.sin(t)*r)
            for t in (i*math.tau/80 for i in range(80))
            for r in [1 + .07*math.sin(3*t+phase) + .035*math.sin(7*t-phase) +
                      .018*math.sin(13*t+phase)]]


def foliage(p, x, y, width, height, rnd, distant=False):
    colors = ([(32, 56, 61, 255), (39, 65, 68, 255), (48, 76, 75, 255)] if distant else
              [(27, 50, 53, 255), (38, 65, 61, 255), (49, 79, 67, 255), (69, 94, 76, 255)])
    for _ in range(max(7, int(width / 4))):
        dx = rnd.uniform(-width/2, width/2)
        dy = rnd.uniform(-height, 0) * math.sqrt(max(0, 1-(2*dx/width)**2))
        rx, ry = rnd.uniform(6, 17), rnd.uniform(4, 10)
        p.poly(rock_outline(x+dx, y+dy, rx, ry, rnd.uniform(0, 6)), rnd.choice(colors))
    for _ in range(int(width*1.2)):
        dx = rnd.uniform(-width/2, width/2)
        dy = rnd.uniform(-height, 0) * math.sqrt(max(0, 1-(2*dx/width)**2))
        p.oval([x+dx, y+dy, x+dx+2, y+dy+1], (102, 123, 92, 100 if distant else 160))


def root(p, a, b, c, d, width=3):
    pts = bezier(a, b, c, d, 3)
    p.line(pts, (22, 36, 38, 255), width+2)
    p.line(pts, (69, 77, 62, 255), width)
    p.line([(x-.5, y-.5) for x, y in pts], (104, 113, 85, 255), .7)


def surface(p, chain, rnd, platform=False):
    # The complete sampled collision polyline is the top of the moss, even at joins.
    top = [(x, H-y) for x, y in chain]
    bottom = []
    for x, y in top:
        t = (x-top[0][0]) / (top[-1][0]-top[0][0])
        depth = (7+15*math.sin(math.pi*t)**.5 if platform else 22)
        bottom.append((x, y+depth+3*math.sin(x*.063)+2*math.sin(x*.19)))
    p.poly(top + bottom[::-1], (38, 61, 54, 255))
    p.line([(x, y+5) for x, y in top], (72, 98, 74, 255), 8)
    p.line([(x, y+1.4) for x, y in top], (137, 155, 112, 255), 2.8)
    p.line([(x, y+.5) for x, y in top], (183, 185, 138, 255), 1)
    for (x0, y0), (x1, y1) in zip(top, top[1:]):
        for _ in range(max(1, int((x1-x0)*2))):
            t = rnd.random()
            x, y = x0+(x1-x0)*t, y0+(y1-y0)*t
            dy = rnd.uniform(3, 17 if not platform else 9)
            p.oval([x, y+dy, x+rnd.uniform(.7, 3), y+dy+1.2],
                   rnd.choice([(123, 139, 95, 255), (51, 83, 61, 255), (83, 116, 80, 255)]))
    for i in range(1, len(top)-1, 4):
        x, y = top[i]
        endx, endy = top[min(i+4, len(top)-1)]
        root(p, (x, y+13), (x+20, y+31), (endx-25, endy+9), (endx, endy+17), 2)
        if rnd.random() < .7:
            length = rnd.uniform(25, 85 if platform else 110)
            vine = bezier((x, y+12), (x-22, y+length*.4), (x+18, y+length*.8), (x-5, y+length), 4)
            p.line(vine, (49, 79, 62, 255), 1.3)
            for j, (vx, vy) in enumerate(vine[2::2]):
                side = 1 if j%2 else -1
                p.oval([vx-3+(side*3), vy-1, vx+3+(side*3), vy+2], (66, 98, 72, 255))


def shrine(p, x, y):
    glow(p, [([(x, y - 63), (x, y - 52)], (255, 133, 69, 135), 27)], 16)
    p.poly([(x - 15, y), (x - 12, y - 6), (x + 12, y - 6), (x + 15, y)], (104, 116, 115, 255))
    p.box([x - 5, y - 45, x + 5, y - 6], (43, 56, 65, 255))
    p.line([(x - 4, y - 43), (x - 4, y - 8)], (126, 150, 146, 255), 1)
    p.box([x - 12, y - 74, x + 12, y - 45], (24, 40, 48, 255))
    p.box([x - 9, y - 71, x + 9, y - 49], (237, 152, 84, 255))
    p.box([x - 5, y - 69, x + 5, y - 51], (255, 213, 142, 255))
    for dx in (-10, 0, 10):
        p.line([(x + dx, y - 74), (x + dx, y - 45)], (57, 57, 61, 255), 2)
    p.poly([(x - 22, y - 74), (x - 13, y - 81), (x, y - 89), (x + 13, y - 81), (x + 22, y - 74)], (47, 63, 72, 255))
    p.line([(x - 21, y - 75), (x, y - 85), (x + 21, y - 75)], (165, 174, 153, 255), 1)
    p.box([x - 15, y - 46, x + 15, y - 42], (88, 105, 108, 255))


def torii(p, x, y):
    glow(p, [([(x - 42, y - 140), (x + 42, y - 140)], (246, 130, 78, 130), 15)], 20)
    for dx in (-46, 46):
        p.poly([(x + dx - 9, y), (x + dx - 6, y - 149), (x + dx + 6, y - 149), (x + dx + 9, y)], (88, 48, 52, 255))
        p.line([(x + dx - 5, y - 146), (x + dx - 8, y - 5)], (177, 105, 94, 255), 2)
        p.box([x + dx - 13, y - 8, x + dx + 13, y], (60, 75, 83, 255))
    p.box([x - 66, y - 122, x + 66, y - 112], (137, 72, 66, 255))
    p.line([(x - 64, y - 121), (x + 64, y - 121)], (218, 150, 117, 255), 1)
    p.poly([(x - 77, y - 161), (x - 45, y - 153), (x + 45, y - 153), (x + 77, y - 161),
            (x + 71, y - 143), (x - 71, y - 143)], (31, 43, 55, 255))
    p.line([(x - 75, y - 160), (x - 43, y - 151), (x + 43, y - 151), (x + 75, y - 160)], (163, 157, 137, 255), 2)
    p.line([(x - 62, y - 141), (x + 62, y - 141)], (255, 173, 104, 255), 2)
    p.box([x - 10, y - 145, x + 10, y - 108], (49, 57, 65, 255))
    for sy in (y - 136, y - 125):
        p.line([(x - 4, sy), (x + 4, sy), (x, sy + 7)], (235, 178, 114, 255), 1.4)
    for dx in (-31, 31):
        p.line([(x + dx, y - 112), (x + dx, y - 88)], (107, 114, 105, 255), 1)
        p.poly([(x + dx - 3, y - 89), (x + dx + 4, y - 86), (x + dx, y - 73), (x + dx - 5, y - 77)], (187, 195, 180, 255))


def painted_ground(level):
    p = Paint(level["width"], H)
    rnd = random.Random(7)
    mask = Image.new("L", p.image.size)
    for chain in level["ground"]:
        outline = chain + [(chain[-1][0], 0), (chain[0][0], 0)]
        ImageDraw.Draw(mask).polygon(p.points([(x, H-y) for x, y in outline]), fill=255)
    body = gradient(level["width"], H, [(0, (55, 66, 64, 255)), (.58, (57, 70, 67, 255)),
                                      (.8, (31, 46, 48, 255)), (1, (14, 27, 38, 255))])
    stone = Paint(level["width"], H)
    for _ in range(500):
        x, y = rnd.uniform(-100, level["width"]+100), rnd.uniform(430, 800)
        rx, ry, phase = rnd.uniform(24, 130), rnd.uniform(12, 58), rnd.uniform(0, 6)
        shade = rnd.randint(-8, 10) - int((y-430)*.035)
        stone.poly(rock_outline(x, y, rx, ry, phase), (35+shade, 51+shade, 55+shade, 255))
        contour = rock_outline(x-2, y-3, rx*.92, ry*.84, phase)
        stone.line(contour[42:72], (65+shade, 77+shade, 73+shade, 180), 1.4)
    body.image.alpha_composite(stone.image.filter(ImageFilter.GaussianBlur(1.2*p.scale)))
    body.draw = ImageDraw.Draw(body.image)
    for chain in (LEFT_BANK, CHECKPOINT_BANK, RIGHT_BANK, LANDING_BANK):
        for depth in (30, 64, 110, 174):
            contour = [(x, H-y+depth+6*math.sin(x*.021+depth)) for x, y in chain]
            body.line(contour, (25, 40, 44, 190), 5)
            body.line([(x, y-3) for x, y in contour], (77, 86, 76, 120), 1)
    grain(body, 17, 38000, [(113, 126, 112, 45), (10, 24, 32, 65), (113, 95, 80, 45)], 9)
    p.image.paste(body.image, (0, 0), mask)
    p.draw = ImageDraw.Draw(p.image)
    roots = Paint(level["width"], H)
    for chain in (LEFT_BANK, CHECKPOINT_BANK, RIGHT_BANK, LANDING_BANK):
        for x, y in chain[::3]:
            if rnd.random() < .3:
                continue
            sy = H-y+10
            reach, bend = rnd.uniform(45, 175), rnd.uniform(-55, 55)
            root(roots, (x, sy), (x+bend, sy+reach*.35), (x-bend*.7, sy+reach*.7),
                 (x+bend*.4, sy+reach), rnd.uniform(1.2, 3))
            if rnd.random() < .6:
                root(roots, (x+bend*.2, sy+reach*.45), (x+bend, sy+reach*.6),
                     (x+bend-25, sy+reach*.75), (x+bend-30, sy+reach*.85), 1)
    roots.image.putalpha(ImageChops.multiply(roots.image.getchannel("A"), mask))
    p.image.alpha_composite(roots.image)
    p.draw = ImageDraw.Draw(p.image)
    for chain in (LEFT_BANK, STEP, CHECKPOINT_BANK, RIGHT_BANK, LANDING_BANK):
        surface(p, chain, rnd)
    for chain in (LEFT_CLIFF, RIGHT_CLIFF, POD_CLIFF, LANDING_CLIFF):
        pts = [(x, H-y) for x, y in chain]
        p.line(pts, (65, 82, 72, 255), 3)
        for i in (1, 3, 6):
            x, y = pts[i]
            root(p, (x, y), (x+19, y+14), (x-15, y+35), (x+4, y+55), 2)
    for x in (40, 510, 680, 985, 1430, 1580, 1770, 1920, 2470, 2610, 2910, 3210, 3420, 3790, 4890, 5100, 5370):
        y = H-ground_height(x)
        foliage(p, x, y+9, rnd.uniform(45, 95), rnd.uniform(20, 43), rnd)
        for j in range(5):
            dx = (j-2)*12
            frond = bezier((x, y+5), (x+dx*.2, y-33), (x+dx, y-40), (x+dx*1.5, y-23), 3)
            p.line(frond, (75, 108, 81, 255), .8)
            for fx, fy in frond[2:-1:2]:
                p.oval([fx-4, fy-1, fx+4, fy+1], (87, 114, 84, 255))
    for chain in level["platforms"]:
        top = [(x, H-y) for x, y in chain]
        branch = [(x, y+12+3*math.sin(math.pi*i/(len(top)-1))) for i, (x, y) in enumerate(top)]
        p.line(branch, (28, 43, 45, 255), 15)
        p.line(branch, (77, 79, 63, 255), 7)
        surface(p, chain, rnd, platform=True)
        a, b = top[0], top[-1]
        # Slack guy cables and hanging roots visually distinguish these one-way ledges.
        for inset in (0, 6):
            cable = catenary((a[0]-15, a[1]-42+inset), (b[0]+15, b[1]-38+inset), 37)
            p.line(cable, (68, 99, 92, 255), 1.4 if inset else 2)
        foliage(p, a[0]+9, a[1]+10, 35, 12, rnd)
        foliage(p, b[0]-9, b[1]+10, 35, 12, rnd)
    for vx, vy, _ in level["cables"]:
        y = H-vy
        branch = bezier((vx-270, y-85), (vx-180, y+15), (vx+40, y-55), (vx+160, y+5), 4)
        p.line(branch, (20, 34, 42, 255), 25)
        p.line([(x, by-4) for x, by in branch], (54, 71, 64, 255), 13)
        p.line([(x, by-8) for x, by in branch], (96, 108, 80, 255), 2)
        p.line(catenary((vx-230, y-65), (vx+105, y-24), 51), (57, 85, 76, 255), 2)
        root(p, (vx-4, y-21), (vx+12, y-16), (vx-12, y-6), (vx, y+2), 5)
        for x, by in branch[10::14]:
            foliage(p, x, by, 85, 30, rnd)
        p.oval([vx-7, y-5, vx+7, y+9], (168, 132, 84, 255))
    for x, y, _, _, _ in level["pods"]:
        # Root-wrapped suspension hoops leave the pod's glowing aim unobstructed.
        sy = H-y
        hoop = [(x+54*math.cos(t), sy+54*math.sin(t)) for t in (i*math.tau/80 for i in range(81))]
        p.line(hoop, (29, 45, 54, 255), 5)
        p.line(hoop[38:74], (107, 123, 109, 255), 1)
        p.line(catenary((x-85, sy-128), (x+65, sy-145), 32), (54, 78, 79, 255), 2)
        root(p, (x-50, sy-113), (x-21, sy-76), (x-48, sy-60), (x-30, sy-45), 3)
    mist = Paint(level["width"], H)
    for x in range(90, level["width"], 370):
        mist.oval([x-80, 633, x+230, 681], (121, 163, 158, 23))
    p.image.alpha_composite(mist.image.filter(ImageFilter.GaussianBlur(20*p.scale)))
    p.draw = ImageDraw.Draw(p.image)
    for cx, cy in level["checkpoints"]:
        shrine(p, cx, H-cy)
    torii(p, level["goal"][0], H-level["goal"][1])
    return p.finish()


def plate(p, pts, color, light=(130, 160, 164, 255)):
    # Local shading and brushed wear stay clipped to each armor or fabric panel.
    left = math.floor(min(x for x, _ in pts))
    top = math.floor(min(y for _, y in pts))
    width = math.ceil(max(x for x, _ in pts)) - left + 1
    height = math.ceil(max(y for _, y in pts)) - top + 1
    panel = Paint(width, height, p.scale)
    for y in range(height * p.scale):
        shade = 1.3 - .65 * y / (height * p.scale)
        tint = tuple(min(255, round(c * shade)) for c in color[:3]) + (255,)
        panel.draw.line([(0, y), (width * p.scale, y)], fill=tint)
    grain(panel, left * 127 + top, max(3, width * height // 10),
          [(181, 203, 199, 25), (8, 17, 30, 45)], 4)
    mask = Image.new("L", panel.image.size)
    ImageDraw.Draw(mask).polygon(panel.points([(x - left, y - top) for x, y in pts]), fill=255)
    p.image.paste(panel.image, (left * p.scale, top * p.scale), mask)
    p.line(pts[:3], light, .65)


def leg(p, hip, knee, ankle, front):
    dark = (30, 45, 57, 255) if front else (21, 30, 43, 255)
    p.line([hip, knee, ankle], dark, 6)
    p.line([(hip[0] + 1, hip[1]), (knee[0] + 1, knee[1])], (64, 81, 92, 255), 2)
    kx, ky = knee
    plate(p, [(kx - 3, ky - 3), (kx + 3, ky - 4), (kx + 4, ky + 2), (kx, ky + 5), (kx - 3, ky + 1)], (68, 80, 88, 255))
    ax, ay = ankle
    p.poly([(ax - 3, ay - 4), (ax + 2, ay - 4), (ax + 4, ay - 1), (ax + 9, ay + 1), (ax + 9, ay + 3), (ax - 4, ay + 3)], dark)
    p.line([(ax - 3, ay + 2), (ax + 8, ay + 2)], (135, 147, 145, 255), .7)
    p.line([(kx + 2, ky + 5), (ax + 2, ay - 4)], (79, 151, 155, 255), 1)


def hero_frame(phase, pose):
    """Kage: 128px frame, drawn at 3x. At scale 48, y=92 is feet=-21."""
    p = Paint(128, 128, 3)
    run = pose == "run"
    wave = math.sin(phase * math.tau)
    bob = (-1 if phase == .5 else 0) if pose == "idle" else (-1 if phase in (.25, .75) else 0)
    lean = 4 if run else 0
    ox, oy = lean, bob
    if run:
        stride = [(((48, 75), (43, 84)), ((77, 75), (85, 89))),
                  (((59, 67), (50, 73)), ((66, 77), (61, 89))),
                  (((76, 75), (83, 89)), ((54, 75), (47, 81))),
                  (((61, 77), (58, 89)), ((74, 69), (69, 78)))][round(phase * 4)]
        legs = [((60 + ox, 61 + oy), *stride[0]), ((65 + ox, 61 + oy), *stride[1])]
    elif pose == "jump":
        legs = [((60, 61), (49, 71), (46, 82)), ((65, 61), (76, 70), (69, 89))]
    elif pose == "fall":
        legs = [((60, 61), (51, 77), (48, 88)), ((65, 61), (69, 76), (73, 89))]
    else:
        legs = [((60, 61 + oy), (57, 76), (54, 89)), ((65, 61 + oy), (67, 76), (69, 89))]
    leg(p, *legs[0], False)
    # Scabbard and the far arm remain behind the coat.
    p.line([(64 + ox, 54 + oy), (32, 79)], (24, 32, 46, 255), 3)
    p.line([(62 + ox, 55 + oy), (34, 77)], (171, 104, 86, 255), .7)
    p.line([(58 + ox, 39 + oy), (46 + ox, 52 + oy), (43 + ox, 58 + oy)], (35, 49, 60, 255), 5)
    leg(p, *legs[1], True)
    tail = 10 if run else 0
    plate(p, [(56 + ox, 44 + oy), (65 + ox, 52 + oy), (57, 72), (37 - tail, 81 - wave * 4),
              (43 - tail, 68), (48, 54)], (35, 46, 61, 255), (79, 101, 111, 255))
    plate(p, [(63 + ox, 48 + oy), (71 + ox, 56), (76, 75), (66, 80), (58, 65)], (42, 56, 68, 255))
    p.poly([(51 + ox, 52), (56 + ox, 57), (49, 71), (37 - tail, 81 - wave * 4), (46, 67)], (96, 49, 67, 255))
    p.line([(48, 62), (42 - tail, 76)], (166, 76, 92, 255), 1)
    plate(p, [(56 + ox, 33 + oy), (66 + ox, 32 + oy), (74 + ox, 39 + oy), (70 + ox, 57 + oy),
              (56 + ox, 58 + oy), (52 + ox, 42 + oy)], (36, 49, 65, 255))
    for yy in (40, 45, 50):
        plate(p, [(56 + ox, yy + oy), (70 + ox, yy - 1 + oy), (69 + ox, yy + 3 + oy), (57 + ox, yy + 4 + oy)],
              (61, 76, 87, 255), (105, 131, 137, 255))
    p.line([(59 + ox, 35 + oy), (65 + ox, 51 + oy)], (167, 115, 88, 255), 1.5)
    p.box([55 + ox, 55 + oy, 71 + ox, 59 + oy], (24, 34, 43, 255))
    p.box([64 + ox, 55 + oy, 67 + ox, 58 + oy], (193, 158, 110, 255))
    # A swept helmet and narrow illuminated visor, with human proportions.
    plate(p, [(57 + ox, 20 + oy), (63 + ox, 17 + oy), (70 + ox, 20 + oy), (73 + ox, 26 + oy),
              (70 + ox, 33 + oy), (59 + ox, 34 + oy), (55 + ox, 28 + oy)], (40, 54, 67, 255))
    p.poly([(59 + ox, 19 + oy), (64 + ox, 15 + oy), (66 + ox, 21 + oy)], (113, 134, 139, 255))
    p.poly([(61 + ox, 26 + oy), (75 + ox, 25 + oy), (72 + ox, 31 + oy), (65 + ox, 33 + oy)], (19, 32, 43, 255))
    glow(p, [([(64 + ox, 26 + oy), (73 + ox, 25 + oy)], (82, 238, 219, 170), 1.5)], 2)
    p.line([(64 + ox, 26 + oy), (73 + ox, 25 + oy)], (175, 255, 230, 255), 1)
    for xx in (65, 68, 71):
        p.line([(xx + ox, 29 + oy), (xx - 1 + ox, 31 + oy)], (97, 121, 132, 255), .5)
    scarf_y = 32 + oy
    p.poly([(61 + ox, scarf_y), (65 + ox, scarf_y + 4), (52, scarf_y + 7), (34 - tail, scarf_y + 2 - wave * 3),
            (19 - tail, scarf_y - 5 - wave * 4), (37, scarf_y - wave * 3), (54, scarf_y + 1)], (113, 52, 72, 255))
    p.line([(59 + ox, scarf_y + 2), (38, scarf_y + 2 - wave * 2), (23 - tail, scarf_y - 3 - wave * 4)], (204, 101, 119, 255), .8)
    # Layered shoulder armor and a braced sword arm.
    plate(p, [(68 + ox, 34 + oy), (75 + ox, 37 + oy), (77 + ox, 44 + oy), (69 + ox, 43 + oy), (66 + ox, 39 + oy)], (78, 91, 101, 255))
    p.line([(73 + ox, 43 + oy), (78 + ox, 53 + oy), (86 + ox, 52 + oy)], (34, 47, 60, 255), 4)
    plate(p, [(78 + ox, 50 + oy), (84 + ox, 49 + oy), (86 + ox, 53 + oy), (79 + ox, 56 + oy)], (87, 103, 109, 255))
    # Katana: quiet steel spine, luminous cutting edge and a small guard.
    grip = (86 + ox, 53 + oy)
    tip = (110 + ox, 26 + oy) if pose != "fall" else (113, 37)
    p.line([(grip[0] - 6, grip[1] + 7), grip], (197, 164, 112, 255), 2)
    p.line([(grip[0] - 4, grip[1] - 2), (grip[0] + 4, grip[1] + 4)], (191, 142, 100, 255), 1.5)
    glow(p, [([grip, tip], (66, 219, 217, 120), 3)], 3)
    p.line([grip, (tip[0] - 5, tip[1] + 8), tip], (88, 160, 173, 255), 2)
    p.line([(grip[0] + 1, grip[1]), (tip[0] - 4, tip[1] + 8), tip], (203, 255, 245, 255), .8)
    return p.finish()


def sentry_frame(step):
    """A low patrol machine, 96x64. Feet at y=60 match the -14 collider bottom."""
    p = Paint(96, 64, 3)
    for i, x in enumerate((24, 64)):
        off = 5 if (i + step) % 2 else -5
        p.line([(x, 35), (x + off, 46), (x + off + 2, 57)], (36, 51, 63, 255), 5)
        plate(p, [(x + off - 5, 43), (x + off + 4, 42), (x + off + 5, 49), (x + off - 3, 50)], (107, 115, 120, 255))
        p.poly([(x + off - 4, 55), (x + off + 5, 55), (x + off + 11, 60), (x + off - 5, 60)], (41, 54, 66, 255))
        p.line([(x + off - 4, 59), (x + off + 10, 59)], (140, 160, 160, 255), 1)
    plate(p, [(15, 25), (25, 14), (63, 15), (80, 26), (77, 39), (60, 47), (25, 44), (14, 36)], (48, 64, 76, 255))
    plate(p, [(24, 18), (60, 18), (69, 26), (48, 30), (21, 28)], (82, 98, 108, 255), (153, 174, 174, 255))
    p.poly([(17, 31), (47, 34), (46, 42), (23, 39)], (25, 38, 51, 255))
    for x in range(23, 43, 5):
        p.line([(x, 33), (x - 1, 38)], (113, 126, 125, 255), 1)
    plate(p, [(61, 26), (79, 25), (84, 30), (80, 38), (64, 38)], (25, 35, 48, 255))
    glow(p, [([(68, 30), (78, 30)], (255, 105, 81, 170), 4)], 5)
    p.line([(68, 30), (79, 30)], (255, 181, 118, 255), 2)
    p.line([(31, 14), (31, 7), (39, 7)], (112, 135, 143, 255), 1)
    p.oval([38, 5, 42, 9], (109, 218, 197, 255))
    return p.finish()


def cable_texture():
    p = Paint(32, 256, 3)
    for x, color in ((10, (32, 44, 60, 255)), (13, (107, 126, 136, 255)), (17, (56, 76, 89, 255)), (20, (17, 29, 43, 255))):
        p.line([(x, 0), (x, 256)], color, 2)
    for y in range(0, 256, 9):
        p.line([(9, y), (22, y + 5)], (125, 144, 148, 255), .8)
    for y in range(16, 256, 48):
        p.box([8, y, 23, y + 6], (45, 63, 76, 255))
        p.line([(10, y + 1), (20, y + 1)], (125, 245, 211, 255), 1.6)
    return p.finish()


def puff():
    p = Paint(64, 64, 3)
    glow(p, [([(32, 28), (32, 36)], (190, 255, 246, 220), 8)], 6)
    p.poly([(32, 5), (35, 28), (45, 32), (35, 35), (32, 59), (29, 35), (19, 32), (29, 29)], (235, 255, 249, 255))
    return p.finish()


def pod_texture(active=False):
    p = Paint(128, 128, 3)
    shell = rock_outline(61, 64, 39, 36, .8)
    glow(p, [([(61, 63), (62, 65)], (68, 212, 192, 120 if active else 65), 65)], 10)
    plate(p, shell, (35, 53, 66, 255), (103, 138, 145, 255))
    # Curved lacquer armor over a sealed seed-shaped chamber; muzzle points right.
    for i in range(4):
        top = bezier((29+i*8, 45-i*3), (46+i*5, 19), (70+i*5, 24), (88+i*2, 49), 2)
        bottom = bezier((88+i*2, 77), (68+i*5, 107), (40+i*5, 102), (29+i*8, 83+i*2), 2)
        p.line(top, (81+i*8, 108+i*6, 116+i*5, 255), 2)
        p.line(bottom, (57+i*5, 76+i*6, 91+i*5, 255), 2)
    seam = bezier((27, 64), (47, 48), (66, 80), (91, 64), 2)
    color = (193, 255, 225, 255) if active else (97, 188, 181, 255)
    glow(p, [(seam, (69, 239, 193, 200), 3)], 5 if active else 2)
    p.line(seam, color, 1.5)
    plate(p, [(82, 50), (106, 56), (111, 64), (106, 72), (82, 78), (89, 64)], (47, 67, 78, 255))
    p.line([(105, 57), (109, 64), (105, 71)], (245, 199, 127, 255), 2)
    p.line([(112, 61), (119, 64), (112, 67)], color, 1)
    for y in (52, 76):
        p.line([(37, y), (47, y-2), (54, y)], (174, 139, 96, 255), 1.3)
    if active:
        glow(p, [(seam, (116, 255, 209, 230), 6),
                 ([(109, 59), (112, 64), (109, 69)], (255, 196, 111, 220), 8)], 8)
        p.line(seam, color, 2)
    p.oval([54, 54, 73, 74], (65, 105, 104, 255) if active else (19, 37, 50, 255))
    p.line([(63, 56), (67, 63), (62, 72), (59, 64), (63, 56)], color, 1.5)
    return p.finish()


def shard_texture():
    p = Paint(48, 72, 3)
    glow(p, [([(25, 25), (23, 47)], (101, 238, 195, 165), 13)], 7)
    plate(p, [(28, 9), (33, 28), (28, 50), (17, 64), (15, 40), (20, 21)], (75, 155, 150, 255))
    p.poly([(28, 9), (25, 34), (17, 64), (15, 40), (20, 21)], (141, 224, 199, 255))
    p.poly([(28, 9), (33, 28), (25, 34)], (218, 247, 219, 255))
    p.line([(27, 13), (24, 34), (18, 59)], (233, 255, 225, 255), 1)
    p.line([(27, 48), (20, 60)], (210, 174, 114, 255), 1)
    return p.finish()


def life_icon():
    p = Paint(48, 56, 3)
    glow(p, [([(24, 19), (24, 35)], (221, 168, 97, 105), 20)], 5)
    plate(p, [(12, 18), (20, 8), (28, 8), (36, 18), (34, 35), (24, 46), (14, 35)], (68, 80, 91, 255))
    p.poly([(14, 23), (34, 23), (31, 34), (24, 40), (17, 34)], (24, 43, 58, 255))
    p.line([(14, 22), (24, 25), (34, 22)], (245, 207, 142, 255), 1.7)
    p.line([(10, 18), (7, 10), (18, 16)], (164, 176, 154, 255), 2)
    p.line([(38, 18), (41, 10), (30, 16)], (164, 176, 154, 255), 2)
    return p.finish()


def main():
    global Image, ImageChops, ImageDraw, ImageFilter
    from PIL import Image, ImageChops, ImageDraw, ImageFilter

    tex = os.path.join(ROOT, "assets", "textures")
    maps = os.path.join(ROOT, "assets", "maps")
    for folder in (tex, maps, os.path.join(ROOT, "art", "kage"), os.path.join(ROOT, "art", "sentry")):
        os.makedirs(folder, exist_ok=True)
    for name, img in (("sky", sky()), ("skyline", city()), ("ruins", city(True)),
                      ("level1", painted_ground(LEVEL1)), ("cable", cable_texture()), ("puff", puff()),
                      ("pod_closed", pod_texture()), ("pod_active", pod_texture(True)),
                      ("shard", shard_texture()), ("hud_shard", shard_texture()), ("hud_life", life_icon())):
        img.save(os.path.join(tex, name + ".png"))
    with open(os.path.join(maps, "level1.tmj"), "w") as f:
        # One line per polyline point, so a map's diff shows the curve that moved.
        text = json.dumps(tiled(LEVEL1, "level1"), indent=1)
        f.write(re.sub(r'\{\s*"x": ([^,]+),\s*"y": ([^\s}]+)\s*\}', r'{"x": \1, "y": \2}', text) + "\n")
    sources = []
    for step in range(2):
        path = f"art/sentry/sentry{step}.png"
        sentry_frame(step).save(os.path.join(ROOT, path))
        sources.append(path)
    frames = {"idle": [0, .5], "run": [0, .25, .5, .75], "jump": [0], "fall": [0]}
    for pose, phases in frames.items():
        for i, phase in enumerate(phases):
            path = f"art/kage/kage_{pose}{i}.png"
            hero_frame(phase, pose).save(os.path.join(ROOT, path))
            sources.append(path)
    with open(os.path.join(ROOT, "assets", "atlases", "characters.atlas.json"), "w") as f:
        json.dump({"sources": sources, "filter": "linear", "padding": 3}, f, indent=2)


if __name__ == "__main__":
    main()
