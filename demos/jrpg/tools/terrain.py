"""Map tiles, 16x16. Ground tiles are textured from noise; paths and water
come in 16 edge variants (bits: 1 north, 2 east, 4 south, 8 west = that side
borders grass) so the map script can auto-tile them."""
import math

from PIL import Image

from pixelart import Canvas, Material, ramp

T = 16


def h2(x, y, seed=0):
    """Deterministic hash noise in 0..1."""
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def smooth(x, y, scale, seed):
    """Value noise, wrapping every 16 px so tiles repeat seamlessly."""
    def corner(cx, cy):
        return h2(cx % (16 // scale), cy % (16 // scale), seed)
    fx, fy = x / scale, y / scale
    ix, iy = int(fx), int(fy)
    tx, ty = fx - ix, fy - iy
    a, b = corner(ix, iy), corner(ix + 1, iy)
    c, d = corner(ix, iy + 1), corner(ix + 1, iy + 1)
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


GRASS = ramp("#1f4a24", "#2e6a2c", "#3e8a34", "#58a83e", "#7cc650")
DIRT = ramp("#5a3a20", "#7a5230", "#9a6c40", "#b88a58", "#d4aa78")
WATER = ramp("#123a78", "#1c56a4", "#2a74c8", "#4a9ae0", "#a8dcff")
SAND = ramp("#9a7a48", "#c4a066", "#e0c088")


def pick(r, v):
    return r[max(0, min(len(r) - 1, int(v)))]


TUFT = ["4..4.", "34.43", ".343.", "12321"]  # blades: tips light, base dark (GRASS indices)


def tall_grass_px(x, y):
    """A tuft in every 8x8 cell, jittered inside its cell (so tiles stay seamless)."""
    cx, cy = x // 8, y // 8
    ox = int(h2(cx, cy, 31) * 3.99)
    oy = int(h2(cx, cy, 32) * 4.99)
    tx, ty = x - cx * 8 - ox, y - cy * 8 - oy
    if 0 <= ty < len(TUFT) and 0 <= tx < len(TUFT[0]) and TUFT[ty][tx] != ".":
        return GRASS[int(TUFT[ty][tx])]
    return GRASS[1] if h2(x, y, 33) > 0.25 else GRASS[0]


def grass_px(x, y, seed=1, tall=False):
    n = smooth(x, y, 4, seed) * 0.6 + h2(x, y, seed + 3) * 0.4
    level = 1.2 + n * 2.0
    if h2(x, y, seed + 7) > (0.86 if not tall else 0.6) and y % 16 > 0:
        level += 1.3            # a lit blade
        if tall and h2(x, y - 1, seed + 7) < 0.5:
            level -= 2.2        # a blade's shadowed base
    if tall and (x * 3 + y * 5) % 7 == 0:
        level -= 0.8
    return pick(GRASS, level)


def dirt_px(x, y):
    n = smooth(x, y, 4, 11) * 0.7 + h2(x, y, 12) * 0.3
    if h2(x, y, 13) > 0.94:
        return DIRT[4]          # pebble highlight
    if h2(x - 1, y - 1, 13) > 0.94:
        return DIRT[0]          # pebble shadow
    return pick(DIRT, 1.4 + n * 1.8)


def tile_img(fn):
    img = Image.new("RGBA", (T, T))
    P = img.load()
    for y in range(T):
        for x in range(T):
            P[x, y] = fn(x, y) + (255,)
    return img


def edge_distance(x, y, mask):
    """Pixels to the nearest grass-bordered side (wobbly), or a large number."""
    d = 99
    wob = lambda a: 1.5 * h2(a, 0, 21)
    if mask & 1: d = min(d, y - wob(x))
    if mask & 4: d = min(d, 15 - y - wob(x + 5))
    if mask & 8: d = min(d, x - wob(y + 9))
    if mask & 2: d = min(d, 15 - x - wob(y + 13))
    return d


def path(mask):
    def px(x, y):
        d = edge_distance(x, y, mask)
        if d < 1.5:
            return grass_px(x, y)
        if d < 2.5:
            return DIRT[0]      # dark rim where the path meets the grass
        return dirt_px(x, y)
    return tile_img(px)


def water(mask, frame):
    def px(x, y):
        d = edge_distance(x, y, mask)
        if d < 1.5:
            return grass_px(x, y, 4)
        if d < 2.5:
            return SAND[1]
        if d < 3.5:
            return WATER[4] if (x + y + frame) % 3 else WATER[3]   # foam
        wave = math.sin((x + frame * 2) * 0.8 + y * 0.35) + math.sin(y * 1.3 - frame) * 0.5
        level = 2.0 + smooth(x, y, 8, 5) * 0.8 - y / 40
        if wave > 1.25:
            level += 1.4
        if h2(x, y, 30 + frame) > 0.985:
            return WATER[4]     # glint
        return pick(WATER, level)
    return tile_img(px)


def on_grass(canvas, seed=1):
    """A shaded object tile over grass."""
    base = tile_img(lambda x, y: grass_px(x, y, seed))
    base.alpha_composite(canvas.render())
    return base


TRUNK = Material(ramp("#2a160a", "#4a2c14", "#6e4420", "#8e5c30"))
LEAVES = Material(ramp("#0e2e14", "#1a4a1e", "#28682a", "#3e8a36", "#62b04a"))
STONE = Material(ramp("#2e3038", "#50545e", "#7a7e8a", "#a4a8b4", "#d0d4dc"))


def tree():
    c = Canvas(T, T)
    c.rect(6.5, 11, 9.5, 16, TRUNK)
    for cx, cy, r in ((8, 6.5, 6.5), (4.5, 8.5, 3.8), (11.5, 8.5, 3.8), (8, 3.5, 4)):
        c.ellipse(cx, cy, r, r * 0.92, LEAVES)
    for i in range(14):  # leaf clumps catch the light
        x, y = 3 + h2(i, 1, 40) * 9, 2 + h2(i, 2, 40) * 8
        c.set(x, y, LEAVES, 0.9 if h2(i, 3, 40) > 0.4 else -0.8)
    return on_grass(c)


def rock():
    c = Canvas(T, T)
    c.ellipse(8, 9.5, 6.5, 5.5, STONE)
    c.ellipse(6, 7.5, 3, 2.5, STONE, 0.6)
    c.line(9, 6, 11, 11, STONE, 1, -1.2)   # a crack
    return on_grass(c, 2)


ROOF = ramp("#4a1010", "#7a1c18", "#a8302a", "#d0503a", "#ec8060")
WOODWALL = ramp("#3a2210", "#5a3a1c", "#7a522a", "#9e6e3c", "#c08e54")
PLASTER = ramp("#8a7c66", "#b0a286", "#d4c8aa", "#ece4cc")


def roof():
    def px(x, y):
        row = y // 4
        lx = (x + (2 if row % 2 else 0)) % 4
        if y % 4 == 3:
            return ROOF[0]
        if lx == 3:
            return ROOF[1]
        level = 3 - (y % 4) * 0.6 + (0.6 if lx == 0 else 0) - (0.4 if h2(x, row, 50) > 0.8 else 0)
        return pick(ROOF, level)
    return tile_img(px)


def wall(window=False, door=False):
    def px(x, y):
        if door and 4 <= x <= 11 and y >= 3:
            if x in (4, 11) or y == 3:
                return WOODWALL[0]
            if x == 10 and y == 10:
                return (240, 200, 80)        # handle
            return pick(WOODWALL, 1 + (0.8 if (x - 5) % 3 == 0 else 0) + (0.4 if x < 7 else 0))
        if window and 4 <= x <= 11 and 3 <= y <= 11:
            if x in (4, 11) or y in (3, 11) or x == 7 or y == 7:
                return WOODWALL[0]
            glow = 1 - (y - 4) / 8
            return (int(120 + 100 * glow), int(170 + 70 * glow), int(200 + 40 * glow))
        if x in (0, 15) or y == 15:
            return WOODWALL[1]               # timber posts and sill
        if y == 0:
            return WOODWALL[2]               # beam under the eaves
        if y == 1:
            return PLASTER[0]                # the eaves' shadow
        return pick(PLASTER, 2.2 - y / 20 + smooth(x, y, 4, 60) * 0.6)
    return tile_img(px)


def fence():
    c = Canvas(T, T)
    rail = Material(WOODWALL)
    c.rect(0, 6, 16, 8, rail)
    c.rect(0, 11, 16, 13, rail)
    c.rect(2, 3, 4, 15, rail)
    c.rect(12, 3, 14, 15, rail)
    return on_grass(c)


def flowers():
    base = tile_img(lambda x, y: grass_px(x, y, 3))
    P = base.load()
    for i, (r, g, b) in enumerate(((240, 90, 140), (250, 220, 90), (240, 240, 255), (180, 120, 240))):
        x, y = 2 + int(h2(i, 9, 70) * 11), 2 + int(h2(i, 8, 70) * 11)
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            P[x + dx, y + dy] = (r, g, b, 255) if (dx, dy) != (0, 0) else (255, 240, 120, 255)
    return base


def bridge():
    def px(x, y):
        if y in (0, 1, 14, 15):
            return WOODWALL[0] if y in (1, 14) else WOODWALL[3]   # rails
        plank = (x // 4)
        if x % 4 == 3:
            return WOODWALL[0]
        return pick(WOODWALL, 2.3 + h2(plank, y // 3, 80) * 0.8 - (0.6 if x % 4 == 2 else 0))
    return tile_img(px)


def cave():
    def px(x, y):
        d = ((x - 7.5) / 6.5) ** 2 + ((y - 16) / 13) ** 2
        if d < 1:
            return (int(40 + 120 * max(0, 1 - d * 1.4)), int(10 + 30 * max(0, 1 - d * 1.4)), 10)  # ember glow
        return pick(STONE.colors, 1.5 + smooth(x, y, 4, 90) * 1.6 + (0.8 if d < 1.25 else 0))
    return tile_img(px)


CHEST = Material(ramp("#3a1a08", "#6a3410", "#9a5418", "#c87a2a"))
TRIM = Material(ramp("#5a3a08", "#9a6a10", "#d8a428", "#f8dc70"))


def chest(open_):
    c = Canvas(T, T)
    c.rect(2, 7, 14, 15, CHEST)
    if open_:
        c.rect(2, 3, 14, 7, CHEST, -0.8)
        c.rect(3, 6, 13, 9, Material(ramp("#100808", "#201010"), shade=False))
    else:
        c.rect(2, 4, 14, 8, CHEST, 0.4)
        c.rect(7, 7, 9, 10, TRIM)
    c.rect(2, 9, 14, 10, TRIM)
    return c.render()


def crystal(frame):
    c = Canvas(T, T)
    glow = Material(ramp("#1a4a8a", "#3a8ad8", "#7ac4ff", "#d0f0ff", "#ffffff"), light=frame * 0.4)
    c.poly([(8, 0), (13, 7), (8, 15), (3, 7)], glow)
    c.line(8, 1, 8, 14, glow, 1, 1.2)
    return c.render()


def shadow():
    img = Image.new("RGBA", (16, 6))
    P = img.load()
    for y in range(6):
        for x in range(16):
            if ((x - 7.5) / 7) ** 2 + ((y - 2.5) / 2.6) ** 2 < 1:
                P[x, y] = (8, 12, 24, 150)
    return img


def all_tiles():
    """name -> Image for every map tile."""
    out = {"grass": tile_img(grass_px), "tall_grass": tile_img(tall_grass_px),
           "tree": tree(), "rock": rock(), "roof": roof(), "wall": wall(), "window": wall(window=True),
           "door": wall(door=True), "fence": fence(), "flowers": flowers(), "bridge": bridge(), "cave": cave(),
           "chest": chest(False), "chest_open": chest(True), "crystal_0": crystal(0), "crystal_1": crystal(1),
           "shadow": shadow()}
    for m in range(16):
        out[f"path_{m}"] = path(m)
        for f in range(3):
            out[f"water_{m}_{f}"] = water(m, f)
    return out
