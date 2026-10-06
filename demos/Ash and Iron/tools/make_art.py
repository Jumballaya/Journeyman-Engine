#!/usr/bin/env python3
"""Ash and Iron's art and audio, drawn from code into tools/out/ (a workbench:
import what you need into the project from the editor, like art from any
other tool). People are cut from Antifarea's CC-BY sheet (see CREDITS.md);
everything else is original. python3 tools/make_art.py (needs Pillow, numpy)."""
import math
import os
import wave

import numpy as np
from PIL import Image, ImageDraw

from pixelart import Canvas, Material, ramp

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
T = 16


def save(folder, name, img):
    path = os.path.join(OUT, folder, name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


# ---- noise ---------------------------------------------------------------------

def h2(x, y, seed=0):
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def smooth(x, y, scale, seed):
    def corner(cx, cy):
        return h2(cx % (16 // scale), cy % (16 // scale), seed)
    fx, fy = x / scale, y / scale
    ix, iy = int(fx), int(fy)
    tx, ty = fx - ix, fy - iy
    a, b = corner(ix, iy), corner(ix + 1, iy)
    c, d = corner(ix, iy + 1), corner(ix + 1, iy + 1)
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def pick(r, v):
    return r[max(0, min(len(r) - 1, int(v)))]


def tile_img(fn, w=T, h=T):
    img = Image.new("RGBA", (w, h))
    P = img.load()
    for y in range(h):
        for x in range(w):
            c = fn(x, y)
            P[x, y] = c if len(c) == 4 else c + (255,)
    return img


# ---- palette: ash, rust, soot, a little teal ----------------------------------------

ASH = ramp("#2c2622", "#3e352e", "#524638", "#665846", "#7e6e58")
GRASS = ramp("#3a3420", "#524a28", "#6c6230", "#8a7c3c", "#a89650")
GRAVEL = ramp("#3a3836", "#54504a", "#6e6860", "#8a8478", "#a8a094")
COBBLE = ramp("#2e2c2c", "#4a4644", "#64605a", "#807a72", "#a09a90")
WATER = ramp("#16241e", "#1e3a30", "#2a5244", "#3c6e5a", "#78a890")
SLAG = ramp("#3a0e06", "#7a1e08", "#c43e0c", "#f07a1e", "#ffd060")
WOOD = ramp("#2a1a10", "#422a18", "#5e3e22", "#7a5430", "#96703e")
METAL = ramp("#1e2226", "#30363c", "#46505a", "#626e7a", "#8a98a4")
RUST = ramp("#3a1a0e", "#5e2a12", "#86401a", "#ac5a24", "#d07a34")
BRICK = ramp("#2e1612", "#4a221a", "#6a3424", "#8a4832", "#a86444")


def ash_px(x, y, seed=1):
    n = smooth(x, y, 4, seed) * 0.6 + h2(x, y, seed + 3) * 0.4
    if h2(x, y, seed + 9) > 0.95:
        return ASH[4]
    if h2(x - 1, y - 1, seed + 9) > 0.95:
        return ASH[0]
    return pick(ASH, 1.2 + n * 2.2)


def grass_px(x, y):
    n = smooth(x, y, 4, 21) * 0.6 + h2(x, y, 22) * 0.4
    level = 1.0 + n * 2.0
    if h2(x, y, 23) > 0.8 and y % 16:
        level += 1.6
    if h2(x, y + 1, 23) > 0.8:
        level -= 1.0
    return pick(GRASS, level)


def gravel_px(x, y):
    n = smooth(x, y, 2, 31) * 0.5 + h2(x, y, 32) * 0.5
    if h2(x, y, 33) > 0.9:
        return GRAVEL[4]
    return pick(GRAVEL, 1.0 + n * 2.6)


def edge_distance(x, y, mask):
    d = 99
    wob = lambda a: 1.5 * h2(a, 0, 41)
    if mask & 1: d = min(d, y - wob(x))
    if mask & 4: d = min(d, 15 - y - wob(x + 5))
    if mask & 8: d = min(d, x - wob(y + 9))
    if mask & 2: d = min(d, 15 - x - wob(y + 13))
    return d


def road(mask):
    def px(x, y):
        d = edge_distance(x, y, mask)
        if d < 1.5:
            return ash_px(x, y)
        if d < 2.5:
            return GRAVEL[0]
        return gravel_px(x, y)
    return tile_img(px)


def water(mask, frame):
    def px(x, y):
        d = edge_distance(x, y, mask)
        if d < 1.5:
            return ash_px(x, y, 5)
        if d < 2.5:
            return ASH[0]
        wave = math.sin((x + frame * 2) * 0.7 + y * 0.4) + math.sin(y * 1.2 - frame) * 0.5
        level = 1.8 + smooth(x, y, 8, 7) * 0.8
        if wave > 1.3:
            level += 1.3
        if h2(x, y, 50 + frame) > 0.985:
            return WATER[4]
        return pick(WATER, level)
    return tile_img(px)


def slag(frame):
    """Dark crust split by glowing cracks (a wrapping Voronoi web) that pulse between frames."""
    pts = [(3.5, 4), (11.5, 2.5), (7, 10), (14, 11.5), (1.5, 13.5)]  # placed: random ones can bunch up

    def edge(x, y):
        ds = []
        for px_, py_ in pts:
            for ox in (-16, 0, 16):
                for oy in (-16, 0, 16):
                    ds.append(math.hypot(x + 0.5 - px_ - ox, y + 0.5 - py_ - oy))
        ds.sort()
        return ds[1] - ds[0]  # 0 on a crack

    glow = 1.0 if frame == 0 else 0.75

    def px(x, y):
        e = edge(x, y)
        if e < 0.8:
            return pick(SLAG, 2.4 + glow * 1.6 - e)
        if e < 1.8:
            return pick(SLAG, 0.4 + glow * 0.9 - (e - 0.8))
        n = smooth(x, y, 4, 64) * 0.7 + h2(x, y, 65) * 0.3
        return pick(ASH, n * 1.4)
    return tile_img(px)


def cobble():
    def px(x, y):
        # Offset stones: 8x5 with mortar lines.
        row = y // 5
        sx = (x + (4 if row % 2 else 0)) % 8
        sy = y % 5
        if sx == 0 or sy == 0:
            return COBBLE[0]
        n = h2((x + (4 if row % 2 else 0)) // 8, row, 71)
        level = 1.5 + n * 2.0 + (0.8 if sy == 1 else 0) - (0.6 if sy == 4 else 0)
        return pick(COBBLE, level)
    return tile_img(px)


def planks(r, seed):
    def px(x, y):
        board = y // 4
        if y % 4 == 0:
            return r[0]
        seam = (x + board * 7) % 16 == 0
        if seam:
            return r[1]
        n = smooth(x, y, 2, seed + board) * 0.5 + h2(x, y, seed) * 0.3
        return pick(r, 1.6 + n * 2.0)
    return tile_img(px)


def metal_floor():
    def px(x, y):
        if x % 8 == 0 or y % 8 == 0:
            return METAL[0]
        if (x % 8, y % 8) in ((2, 2), (6, 6)):
            return METAL[4]
        return pick(METAL, 2.0 + h2(x, y, 81) * 0.8 - y % 8 * 0.08)
    return tile_img(px)


def brick_wall(window=False, door=False):
    def px(x, y):
        row = y // 4
        bx = (x + (4 if row % 2 else 0)) % 8
        if y % 4 == 0 or bx == 0:
            c = BRICK[0]
        else:
            c = pick(BRICK, 1.5 + h2((x + (4 if row % 2 else 0)) // 8, row, 91) * 2)
        if window and 4 <= x <= 11 and 3 <= y <= 10:
            if x in (4, 11) or y in (3, 10):
                c = WOOD[1]
            else:
                c = (230, 170, 80) if (x + y) % 5 else (255, 210, 120)   # lamplight
        if door and 4 <= x <= 11 and y >= 4:
            if x in (4, 11) or y == 4:
                c = WOOD[0]
            else:
                c = pick(WOOD, 1.6 + (0.6 if x % 3 == 0 else 0))
                if (x, y) == (9, 10):
                    c = (200, 170, 90)
        return c
    return tile_img(px)


def roof():
    def px(x, y):
        ridge = x % 4
        level = [1.0, 2.6, 2.0, 1.4][ridge] + h2(x // 4, y, 101) * 0.6
        if h2(x, y // 3, 102) > 0.8:
            return pick(RUST, level)         # rust streaks on corrugated tin
        return pick(METAL, level + 0.3)
    return tile_img(px)


def metal_wall():
    def px(x, y):
        if y % 8 == 0:
            return METAL[0]
        if x % 8 == 3 and y % 8 in (2, 6):
            return METAL[4]
        rust = h2(x // 2, y // 2, 111) > 0.82
        return pick(RUST if rust else METAL, 1.4 + smooth(x, y, 4, 112) * 1.5)
    return tile_img(px)


def on(base_fn, canvas):
    img = tile_img(base_fn)
    img.alpha_composite(canvas.render())
    return img


def decor(canvas, shadow=(8, 13, 6, 2.2)):
    """A prop on transparency, grounded by a soft shadow: the map draws the ground under it."""
    img = Image.new("RGBA", (T, T))
    if shadow:
        sh = Image.new("RGBA", (T, T))
        cx, cy, rx, ry = shadow
        ImageDraw.Draw(sh).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=(10, 6, 4, 90))
        img.alpha_composite(sh)
    img.alpha_composite(canvas.render())
    return img


ROCK = Material(ramp("#24201e", "#3e3832", "#5a5248", "#766c60", "#968a7a"))
BARK = Material(ramp("#1e140e", "#33241a", "#4a3626", "#624a34"))
DRY = Material(ramp("#2e2a14", "#4a4420", "#6a6030", "#8a7e40"))
RUSTM = Material(RUST)
METALM = Material(METAL)
WOODM = Material(WOOD)


def rock():
    c = Canvas(T, T)
    c.ellipse(8, 9.5, 6.5, 5, ROCK)
    c.ellipse(5, 7, 3, 2.5, ROCK, 0.6)
    return decor(c, (8, 13.5, 7, 2))


def dead_tree():
    c = Canvas(T, T)
    c.line(8, 15, 8, 4, BARK, 2.6)
    c.line(8, 9, 4, 5, BARK, 1.5)
    c.line(8, 7, 12, 3, BARK, 1.5)
    c.line(4, 5, 3, 2, BARK, 1)
    c.line(12, 3, 14, 2, BARK, 1)
    return decor(c, (8, 14.5, 4, 1.4))


def scrub():
    c = Canvas(T, T)
    for i in range(7):
        a = -60 + i * 20
        c.limb(8, 13, 180 + a, 6 + (i % 3), 1.2, DRY)
    return decor(c, (8, 13.5, 5, 1.6))


def fence():
    c = Canvas(T, T)
    c.rect(0, 6, 16, 8, RUSTM)
    c.rect(0, 10, 16, 12, RUSTM)
    for x in (1, 7, 13):
        c.rect(x, 3, x + 2, 15, METALM)
    return decor(c, None)


def barrel():
    c = Canvas(T, T)
    c.ellipse(8, 9, 5.5, 6.5, RUSTM)
    c.rect(2.5, 6, 13.5, 7, METALM)
    c.rect(2.5, 11, 13.5, 12, METALM)
    return decor(c, (8, 15, 5.5, 1.4))


def crate():
    c = Canvas(T, T)
    c.rect(2, 3, 14, 14, WOODM)
    c.line(2, 3, 14, 14, WOODM, 1, -0.8)
    c.line(14, 3, 2, 14, WOODM, 1, -0.8)
    return decor(c, (8, 14.6, 6.5, 1.4))


def scrap():
    c = Canvas(T, T)
    c.poly([(2, 13), (6, 7), (10, 9), (14, 13)], METALM)
    c.rect(5, 8, 9, 10, RUSTM)
    c.line(8, 4, 11, 10, METALM, 1.2)
    c.ellipse(11, 11, 2, 2, RUSTM)
    return decor(c, (8, 13.6, 6.5, 1.6))


def rubble():
    c = Canvas(T, T)
    for i in range(6):
        cx, cy = 2 + h2(i, 0, 121) * 12, 3 + h2(i, 1, 121) * 11
        c.ellipse(cx, cy, 1.2 + h2(i, 2, 121), 1 + h2(i, 3, 121) * 0.8, ROCK)
    return decor(c, None)


def bridge():
    def px(x, y):
        if x in (0, 15):
            return WOOD[0]
        return pick(WOOD, 1.8 + h2(x, y // 3, 131) * 1.5 - (1.2 if y % 3 == 0 else 0))
    return tile_img(px)


def gate(open_):
    c = Canvas(T, T)
    c.rect(0, 0, 3, 16, METALM)
    c.rect(13, 0, 16, 16, METALM)
    if not open_:
        for x in range(4, 13, 3):
            c.rect(x, 1, x + 1, 16, RUSTM)
        c.rect(3, 4, 13, 5, RUSTM)
        c.rect(3, 11, 13, 12, RUSTM)
    return on(gravel_px, c)


def tiles():
    save("tiles", "ash", tile_img(ash_px))
    save("tiles", "dry_grass", tile_img(grass_px))
    save("tiles", "cobble", cobble())
    save("tiles", "floor_wood", planks(WOOD, 141))
    save("tiles", "floor_metal", metal_floor())
    save("tiles", "wall_brick", brick_wall())
    save("tiles", "wall_window", brick_wall(window=True))
    save("tiles", "wall_door", brick_wall(door=True))
    save("tiles", "roof", roof())
    save("tiles", "metal_wall", metal_wall())
    save("tiles", "rock", rock())
    save("tiles", "dead_tree", dead_tree())
    save("tiles", "scrub", scrub())
    save("tiles", "fence", fence())
    save("tiles", "barrel", barrel())
    save("tiles", "crate", crate())
    save("tiles", "scrap", scrap())
    save("tiles", "rubble", rubble())
    save("tiles", "bridge", bridge())
    save("tiles", "gate_closed", gate(False))
    save("tiles", "gate_open", gate(True))
    for f in range(2):
        save("tiles", f"slag_{f}", slag(f))
    for m in range(16):
        save("tiles", f"road_{m}", road(m))
        for f in range(3):
            save("tiles", f"water_{m}_{f}", water(m, f))


# ---- people (Antifarea, CC-BY 3.0) -------------------------------------------------

FW, FH = 18, 20
CAST = {"player": (2, 0), "elder": (19, 0), "merchant": (11, 0), "doctor": (3, 1), "guard": (4, 0),
        "kid": (12, 0), "raider": (14, 1), "raider2": (9, 1), "villager": (7, 0), "villager2": (16, 1)}
ROWS = {"up": 0, "side": 1, "down": 2}
_sheet = None


def frame(who, row, col):
    global _sheet
    if _sheet is None:
        _sheet = Image.open(os.path.join(HERE, "vendor", "antifarea_charset_18x20.png")).convert("RGBA")
    band, half = CAST[who]
    x = half * 12 * FW + col * FW
    y = band * 3 * FH + ROWS[row] * FH
    img = _sheet.crop((x, y, x + FW, y + FH))
    return img.transpose(Image.FLIP_LEFT_RIGHT) if row == "side" else img  # faces right


def walker(who, facing, step):
    img = frame(who, facing, (1, 0, 2)[step])
    box = img.getbbox()
    cx = (box[0] + box[2]) // 2
    left = max(0, min(FW - 16, cx - 8))
    out = Image.new("RGBA", (16, 24))
    out.paste(img.crop((left, 0, left + 16, FH)), (0, 24 - FH))
    return out


def portrait(who):
    img = frame(who, "down", 1)
    box = img.getbbox()
    cx = (box[0] + box[2]) // 2
    head = img.crop((cx - 8, 0, cx + 8, 16)).resize((32, 32), Image.NEAREST)
    out = Image.new("RGBA", (32, 32), (36, 30, 26, 255))
    out.alpha_composite(head)
    return out


def people():
    for who in CAST:
        for facing in ROWS:
            for step in range(3):
                save("people", f"{who}_{facing}_{step}", walker(who, facing, step))
        save("portraits", f"portrait_{who}", portrait(who))


# ---- the Iron Warden and its drones (original) ---------------------------------------

IRON = Material(ramp("#141618", "#262a2e", "#3c4248", "#58606a", "#7c8692"))
BRASS = Material(ramp("#3a2a10", "#5e4418", "#8a6626", "#b48a36", "#dcb054"))
GLOW = Material(ramp("#7a1e08", "#d04a10", "#ff8a2a", "#ffd070"), shade=False, outline=False)
TEAL = Material(ramp("#0e2a2a", "#1a4a48", "#2a706a", "#48a094", "#8ad8c8"))


def warden(pose):
    """32x32, facing down. pose: 0/1 idle (furnace flickers), 2 swinging."""
    c = Canvas(32, 32)
    c.ellipse(16, 29, 9, 2.5, IRON, -1.5)           # shadow-ish base
    c.rect(10, 22, 14, 30, IRON)                      # legs
    c.rect(18, 22, 22, 30, IRON)
    c.ellipse(16, 16, 9.5, 8.5, IRON)                 # boiler body
    c.ellipse(16, 17, 4, 3.5, GLOW, 0.3 if pose == 1 else 0)   # furnace window
    c.rect(11, 9, 21, 11, BRASS)                      # collar
    c.ellipse(16, 6, 5, 4.5, IRON, 0.4)               # head
    c.rect(13, 5, 19, 7, GLOW)                        # visor
    c.rect(14, 0, 15, 3, BRASS)                       # smokestack
    arm = 30 if pose == 2 else 10
    c.limb(7, 13, arm, 9, 3.4, IRON)                  # arms
    c.limb(25, 13, -arm, 9, 3.4, IRON)
    end = c.limb(25, 13, -arm, 9, 0.1, IRON)
    c.ellipse(end[0], end[1], 3, 3, BRASS)            # hammer fist
    c.ellipse(7 + math.sin(math.radians(arm)) * 9, 13 + math.cos(math.radians(arm)) * 9, 2.5, 2.5, BRASS)
    return c.render()


def drone(frame_):
    c = Canvas(16, 16)
    c.ellipse(8, 8 + frame_, 5, 4, IRON)
    c.rect(1, 6 + frame_, 15, 7 + frame_, BRASS)
    c.ellipse(8, 8 + frame_, 1.6, 1.6, GLOW)
    c.line(8, 3 + frame_, 8, 1 + frame_, BRASS)
    return c.render()


def machines():
    for p in range(3):
        save("people", f"warden_{p}", warden(p))
    for f in range(2):
        save("people", f"drone_{f}", drone(f))
    big = warden(0).resize((32, 32))
    pic = Image.new("RGBA", (32, 32), (36, 30, 26, 255))
    pic.alpha_composite(big)
    save("portraits", "portrait_warden", pic)


# ---- items, abilities, effects, UI marks ----------------------------------------------

RED = Material(ramp("#3a0a0a", "#7a1414", "#b42222", "#e04a3a", "#ff8a7a"))
GLASS = Material(ramp("#3a4a52", "#6a8a94", "#a8c8d0"), outline=True)
GOLD = Material(ramp("#4a3008", "#7a5410", "#b08018", "#e0b030", "#fff070"))
LEATHER = Material(ramp("#2a1a0e", "#4a2e18", "#6e4626", "#946038"))
GREEN = Material(ramp("#0e2a12", "#1a5020", "#2e8034", "#56b850", "#a0f08a"))
WHITE = Material(ramp("#8a8a8a", "#c8c8c8", "#ffffff"), shade=False)
EMBER = Material(ramp("#5a1206", "#a82a08", "#e8601a", "#ffa040", "#ffe08a"))


def icon(draw):
    c = Canvas(T, T)
    draw(c)
    return c.render()


def items():
    def tonic(c):
        c.rect(6, 2, 10, 5, GLASS)
        c.ellipse(8, 10, 5, 5, GLASS)
        c.ellipse(8, 11, 4, 3.5, RED)
    def ember_shard(c):
        c.poly([(8, 1), (13, 8), (8, 15), (3, 8)], EMBER)
    def wrench(c):
        c.line(3, 13, 11, 5, METALM, 2.2)
        c.ellipse(12, 4, 3, 3, METALM)
    def revolver(c):
        c.rect(3, 5, 14, 8, METALM)
        c.ellipse(7, 7, 2.5, 2.5, METALM, 0.4)
        c.poly([(3, 7), (6, 7), (5, 13), (2, 13)], WOODM)
    def coat(c):
        c.poly([(4, 2), (12, 2), (14, 14), (2, 14)], LEATHER)
        c.line(8, 3, 8, 14, LEATHER, 1, -1.2)
    def key(c):
        c.ellipse(5, 5, 3, 3, BRASS)
        c.line(7, 7, 13, 13, BRASS, 1.6)
        c.rect(10, 11, 12, 13, BRASS)
    def gear(c):
        for i in range(8):
            a = i * math.pi / 4
            c.ellipse(8 + math.cos(a) * 5.5, 8 + math.sin(a) * 5.5, 1.6, 1.6, BRASS)
        c.ellipse(8, 8, 5, 5, BRASS)
        c.ellipse(8, 8, 1.8, 1.8, IRON)
    def locket(c):
        c.line(4, 2, 8, 7, GOLD, 1)
        c.line(12, 2, 8, 7, GOLD, 1)
        c.ellipse(8, 10, 4, 4.5, GOLD)
    def coins(c):
        for (x, y) in ((5, 11), (10, 11), (7.5, 7)):
            c.ellipse(x, y, 3.5, 2.5, GOLD)
    def ammo(c):
        for x in (4, 8, 12):
            c.rect(x - 1, 5, x + 1, 13, BRASS)
            c.rect(x - 1, 3, x + 1, 5, METALM)
    def bandage(c):
        c.rect(3, 5, 13, 11, WHITE)
        c.rect(7, 5, 9, 11, RED)
    def cog_core(c):
        c.ellipse(8, 8, 6, 6, IRON)
        c.ellipse(8, 8, 3, 3, GLOW)
    for name, fn in [("tonic", tonic), ("ember_shard", ember_shard), ("wrench", wrench), ("revolver", revolver),
                     ("duster", coat), ("key", key), ("gear", gear), ("locket", locket), ("coins", coins),
                     ("ammo", ammo), ("bandage", bandage), ("warden_core", cog_core)]:
        save("icons", f"item_{name}", icon(fn))


def abilities():
    def strike(c):
        c.line(3, 13, 12, 4, METALM, 2)
        c.rect(2, 11, 6, 14, WOODM)
    def aimed(c):
        c.ellipse(8, 8, 6, 6, RED)
        c.ellipse(8, 8, 4, 4, Material(ramp("#141010", "#201818"), shade=False))
        c.rect(7.5, 1, 8.5, 15, WHITE)
        c.rect(1, 7.5, 15, 8.5, WHITE)
    def ember_bolt(c):
        c.poly([(8, 1), (12, 7), (11, 13), (8, 15), (5, 13), (4, 7)], EMBER)
        c.poly([(8, 6), (10, 10), (8, 13), (6, 10)], GLOW)
    def mend(c):
        c.rect(6, 2, 10, 14, GREEN)
        c.rect(2, 6, 14, 10, GREEN)
    def wait(c):
        c.poly([(4, 2), (12, 2), (8, 8)], BRASS)
        c.poly([(8, 8), (12, 14), (4, 14)], BRASS)
    def move(c):
        c.poly([(8, 1), (13, 7), (10, 7), (10, 14), (6, 14), (6, 7), (3, 7)], TEAL)
    for name, fn in [("strike", strike), ("aimed_shot", aimed), ("ember_bolt", ember_bolt), ("mend", mend),
                     ("end_turn", wait), ("move", move)]:
        save("icons", f"ability_{name}", icon(fn))


def effects():
    for f in range(3):
        c = Canvas(T, T)
        r = 2 + f * 2.5
        for i in range(6):
            a = i * math.pi / 3 + f * 0.4
            c.line(8 + math.cos(a) * r * 0.4, 8 + math.sin(a) * r * 0.4, 8 + math.cos(a) * r, 8 + math.sin(a) * r, WHITE)
        save("fx", f"spark_{f}", c.render(outline=False))
        c = Canvas(T, T)
        c.ellipse(8, 8, 3 + f * 1.5, 3 + f * 1.5, EMBER if f < 2 else GLOW)
        save("fx", f"burst_{f}", c.render(outline=False))
        c = Canvas(T, T)
        for i in range(4):
            x = 3 + i * 3.5
            c.rect(x, 12 - f * 4 - i % 2 * 2, x + 1.5, 14 - f * 4 - i % 2 * 2, GREEN)
        save("fx", f"heal_{f}", c.render(outline=False))
    for f in range(2):
        c = Canvas(T, T)
        c.ellipse(9 - f, 8, 4, 2.5, EMBER)
        c.ellipse(5 - f, 8, 2, 1.5, GLOW)
        save("fx", f"bolt_{f}", c.render())
    # Grid marks for turn-based combat (drawn over tiles, translucent).
    def mark(color, border):
        img = Image.new("RGBA", (T, T))
        d = ImageDraw.Draw(img)
        d.rectangle([1, 1, 14, 14], fill=color, outline=border)
        return img
    save("ui", "mark_move", mark((80, 190, 200, 60), (120, 220, 230, 150)))
    save("ui", "mark_attack", mark((220, 70, 50, 70), (255, 120, 90, 180)))
    save("ui", "mark_path", mark((240, 210, 120, 90), (255, 230, 150, 200)))
    cursor = Image.new("RGBA", (T, T))
    d = ImageDraw.Draw(cursor)
    for (x0, y0, dx, dy) in ((0, 0, 1, 1), (15, 0, -1, 1), (0, 15, 1, -1), (15, 15, -1, -1)):
        d.line([x0, y0, x0 + dx * 4, y0], fill=(255, 220, 120, 255))
        d.line([x0, y0, x0, y0 + dy * 4], fill=(255, 220, 120, 255))
    save("ui", "cursor", cursor)
    shadow = Image.new("RGBA", (T, 6))
    ImageDraw.Draw(shadow).ellipse([2, 0, 13, 5], fill=(0, 0, 0, 110))
    save("ui", "shadow", shadow)


# ---- paintings: title and intro (480x270) ---------------------------------------------

def sky(img, top, bottom):
    d = ImageDraw.Draw(img)
    w, h = img.size
    for y in range(h):
        t_ = y / h
        d.line([0, y, w, y], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t_) for i in range(3)))


def skyline(d, base, color, seed, chimneys=True, w=480):
    x = 0
    while x < w:
        bw = 20 + int(h2(x, 0, seed) * 50)
        bh = 18 + int(h2(x, 1, seed) * 60)
        d.rectangle([x, base - bh, x + bw, base], fill=color)
        if chimneys and h2(x, 2, seed) > 0.55:
            cx = x + int(bw * 0.6)
            d.rectangle([cx, base - bh - 30, cx + 5, base - bh], fill=color)
        x += bw + int(h2(x, 3, seed) * 6)


def ash_motes(img, seed, count=180):
    P = img.load()
    w, h = img.size
    for i in range(count):
        x, y = int(h2(i, 0, seed) * w), int(h2(i, 1, seed) * h)
        P[x, y] = (200, 190, 180)


def painting(kind):
    img = Image.new("RGB", (480, 270))
    if kind == "title":
        sky(img, (40, 26, 24), (196, 96, 40))
        d = ImageDraw.Draw(img)
        d.ellipse([300, 150, 380, 230], fill=(240, 150, 70))          # low sun
        skyline(d, 210, (52, 34, 30), 7)
        skyline(d, 235, (30, 20, 18), 11)
        d.rectangle([0, 235, 480, 270], fill=(22, 16, 14))
        ash_motes(img, 3)
    elif kind == "intro_1":   # the town under the foundry's shadow
        sky(img, (30, 30, 36), (110, 70, 52))
        d = ImageDraw.Draw(img)
        skyline(d, 180, (60, 40, 34), 21)
        for x in range(40, 460, 60):                                    # lit windows
            d.rectangle([x, 200, x + 8, 206], fill=(240, 180, 90))
        d.rectangle([0, 210, 480, 270], fill=(34, 26, 22))
        skyline(d, 210, (44, 32, 28), 23, chimneys=False)
        ash_motes(img, 5)
    elif kind == "intro_2":   # raiders on the road
        sky(img, (60, 24, 20), (150, 60, 30))
        d = ImageDraw.Draw(img)
        d.polygon([(0, 270), (200, 160), (280, 160), (480, 270)], fill=(70, 60, 50))
        for i in range(5):
            x = 170 + i * 30
            d.rectangle([x, 120 + (i % 2) * 6, x + 10, 160], fill=(20, 12, 10))
            d.ellipse([x + 1, 108 + (i % 2) * 6, x + 9, 120 + (i % 2) * 6], fill=(20, 12, 10))
        ash_motes(img, 9, 300)
    elif kind == "intro_3":   # the Iron Warden wakes
        sky(img, (12, 10, 12), (40, 20, 18))
        d = ImageDraw.Draw(img)
        d.rectangle([180, 60, 300, 230], fill=(26, 28, 32))
        d.ellipse([200, 30, 280, 100], fill=(30, 32, 36))
        d.rectangle([215, 58, 265, 68], fill=(255, 140, 40))
        d.ellipse([220, 130, 260, 170], fill=(220, 90, 20))
        d.rectangle([0, 230, 480, 270], fill=(18, 14, 12))
        ash_motes(img, 13, 120)
    return img


def paintings():
    for kind in ("title", "intro_1", "intro_2", "intro_3"):
        save("art", kind, painting(kind))


# ---- sound ----------------------------------------------------------------------------

SR = 22050


def t(seconds):
    return np.arange(int(seconds * SR)) / SR


def hz(note):
    if note == "-":
        return 0
    names = {"C": -9, "C#": -8, "Db": -8, "D": -7, "D#": -6, "Eb": -6, "E": -5, "F": -4, "F#": -3, "Gb": -3,
             "G": -2, "G#": -1, "Ab": -1, "A": 0, "A#": 1, "Bb": 1, "B": 2}
    return 440.0 * 2 ** ((names[note[:-1]] + (int(note[-1]) - 4) * 12) / 12)


def square(freq, seconds, duty=0.5, decay=3.0):
    x = t(seconds)
    if freq == 0:
        return np.zeros_like(x)
    return np.where(((x * freq) % 1.0) < duty, 1.0, -1.0) * np.exp(-decay * x)


def tri(freq, seconds):
    x = t(seconds)
    return np.zeros_like(x) if freq == 0 else 2 * np.abs(2 * ((x * freq) % 1.0) - 1) - 1


def sweep(f0, f1, seconds, duty=0.5):
    x = t(seconds)
    phase = np.cumsum(np.geomspace(max(f0, 1), max(f1, 1), len(x)) / SR)
    return np.where((phase % 1.0) < duty, 1.0, -1.0)


def noise(seconds, decay, seed=11):
    x = t(seconds)
    return np.random.default_rng(seed).uniform(-1, 1, len(x)) * np.exp(-decay * x)


def notes(seq, step, duty=0.5, decay=4.0):
    return np.concatenate([square(hz(n), step, duty, decay) for n in seq])


def write(name, samples, gain=0.8, loop=False):
    peak = np.max(np.abs(samples)) or 1.0
    data = np.clip(samples / peak * gain, -1, 1)
    if not loop:
        k = min(len(data), int(0.006 * SR))
        data[len(data) - k:] *= np.linspace(1, 0, k)
    path = os.path.join(OUT, "sounds", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((data * 32767).astype("<i2").tobytes())


def sounds():
    write("ui_move.wav", square(hz("D6"), 0.03, 0.5, 40), 0.3)
    write("ui_select.wav", notes(["G5", "D6"], 0.04, 0.5, 10), 0.4)
    write("ui_back.wav", notes(["D5", "G4"], 0.04, 0.5, 10), 0.4)
    write("step.wav", noise(0.05, 60, 3) * 0.6, 0.25)
    write("hit.wav", noise(0.14, 22) + 0.5 * square(hz("C3"), 0.14, 0.5, 22), 0.6)
    write("crit.wav", noise(0.22, 14) + 0.6 * sweep(900, 180, 0.22), 0.7)
    write("miss.wav", sweep(700, 1100, 0.1, 0.25) * np.exp(-20 * t(0.1)), 0.35)
    write("gunshot.wav", noise(0.25, 18, 5) + 0.4 * sweep(400, 60, 0.25), 0.75)
    write("ember.wav", noise(0.5, 4) * np.sin(np.linspace(0, np.pi, int(0.5 * SR))) + 0.3 * sweep(200, 700, 0.5), 0.6)
    write("mend.wav", notes(["C5", "E5", "G5", "C6", "E6"], 0.06, 0.25, 4), 0.45)
    write("pickup.wav", notes(["E5", "A5", "C#6"], 0.05, 0.5, 6), 0.45)
    write("door.wav", noise(0.3, 8, 7) * 0.5 + 0.5 * sweep(120, 80, 0.3), 0.5)
    write("quest.wav", notes(["C5", "G5", "C6", "-", "E6", "G6"], 0.08, 0.5, 3), 0.5)
    write("level_up.wav", notes(["C5", "E5", "G5", "C6", "-", "G5", "C6"], 0.09, 0.5, 2), 0.5)
    write("combat_start.wav", np.concatenate([sweep(150, 900, 0.3, 0.25), sweep(900, 150, 0.3, 0.25)]), 0.5)
    write("death.wav", sweep(500, 50, 0.5) * np.exp(-4 * t(0.5)) + 0.4 * noise(0.5, 6), 0.6)
    write("warden_roar.wav", sweep(90, 40, 1.2, 0.3) * np.exp(-1.5 * t(1.2)) + 0.5 * noise(1.2, 2, 9), 0.7)
    write("save.wav", notes(["A5", "C#6", "E6", "A6"], 0.08, 0.5, 3), 0.45)
    write("text.wav", square(hz("A5"), 0.02, 0.5, 60), 0.2)


def song(melody, bass, beat, name, duty=0.5):
    lead = np.concatenate([np.concatenate([square(hz(n), beat * b - 0.015, duty, 1.8), np.zeros(int(0.015 * SR))]) for n, b in melody])
    low = np.concatenate([tri(hz(n), beat * b) for n, b in bass])
    n = min(len(lead), len(low))
    write(name, 0.45 * lead[:n] + 0.55 * low[:n], 0.6, loop=True)


def music():
    # Title: slow, wistful, D minor.
    title = [("D5", 2), ("F5", 1), ("E5", 1), ("D5", 2), ("A4", 2), ("Bb4", 2), ("A4", 1), ("G4", 1), ("A4", 4),
             ("D5", 2), ("F5", 1), ("G5", 1), ("A5", 2), ("F5", 2), ("E5", 2), ("C5", 2), ("D5", 4)]
    title_bass = [(n, 2) for n in ["D3", "A2", "D3", "A2", "Bb2", "F3", "A2", "E3", "D3", "A2", "F3", "C3", "A2", "C3", "D3", "A2"]]
    song(title, title_bass, 0.34, "music_title.wav", 0.25)
    # Cinderwell: a tired frontier town, A minor.
    town = [("E5", 1), ("C5", 1), ("D5", 1), ("E5", 1), ("A4", 2), ("C5", 2), ("D5", 1), ("C5", 1), ("B4", 1), ("G4", 1), ("A4", 4),
            ("E5", 1), ("G5", 1), ("A5", 1), ("G5", 1), ("E5", 2), ("D5", 2), ("C5", 1), ("B4", 1), ("G4", 2), ("A4", 4)]
    town_bass = [(n, 2) for n in ["A2", "E3", "A2", "E3", "F2", "C3", "G2", "D3", "A2", "E3", "C3", "G2", "F2", "E2", "A2", "E3"]]
    song(town, town_bass, 0.28, "music_town.wav", 0.25)
    # The Ashen Road: wandering, open.
    road = [("A4", 1), ("B4", 1), ("C5", 2), ("E5", 2), ("D5", 1), ("C5", 1), ("B4", 4),
            ("A4", 1), ("C5", 1), ("E5", 2), ("A5", 2), ("G5", 1), ("E5", 1), ("D5", 4)]
    road_bass = [(n, 2) for n in ["A2", "A2", "C3", "E3", "G2", "G2", "E2", "E2", "A2", "C3", "E3", "A2", "G2", "D3", "D3", "D3"]]
    song(road, road_bass, 0.26, "music_road.wav", 0.5)
    # Combat: driving.
    combat = [("A5", 0.5), ("E5", 0.5), ("A5", 0.5), ("C6", 0.5), ("B5", 0.5), ("A5", 0.5), ("G5", 0.5), ("E5", 0.5)] * 2 + \
             [("F5", 0.5), ("C5", 0.5), ("F5", 0.5), ("A5", 0.5), ("G#5", 0.5), ("E5", 0.5), ("B4", 0.5), ("E5", 0.5)] * 2
    combat_bass = [("A2", 0.5), ("A3", 0.5)] * 8 + [("F2", 0.5), ("F3", 0.5)] * 4 + [("E2", 0.5), ("E3", 0.5)] * 4
    song(combat, combat_bass, 0.16, "music_combat.wav", 0.25)
    # The Warden: heavy, chromatic.
    boss = [("E5", 0.5), ("F5", 0.5), ("E5", 0.5), ("D#5", 0.5), ("E5", 1), ("B4", 1),
            ("C5", 0.5), ("D5", 0.5), ("C5", 0.5), ("B4", 0.5), ("A4", 2)] * 2
    boss_bass = [("E2", 0.5), ("E2", 0.5), ("E3", 0.5), ("E2", 0.5)] * 4 + [("A1", 0.5), ("A2", 0.5)] * 8
    song(boss, boss_bass, 0.18, "music_boss.wav", 0.25)
    # The end of the slice.
    win = [("C5", 1), ("E5", 1), ("G5", 1), ("C6", 3), ("B5", 1), ("G5", 1), ("A5", 4)]
    win_bass = [(n, 2) for n in ["C3", "G2", "C3", "E3", "F2", "G2"]]
    song(win, win_bass, 0.3, "music_victory.wav", 0.5)


if __name__ == "__main__":
    tiles()
    people()
    machines()
    items()
    abilities()
    effects()
    paintings()
    sounds()
    music()
    print("written to", OUT)
