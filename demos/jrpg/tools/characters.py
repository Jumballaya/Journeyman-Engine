"""People, drawn as puppets: a few body parts posed per frame, then shaded by
pixelart.Canvas. Battle figures are 24x32 facing right; map figures are
16x24 chibis facing down, up or right."""
from pixelart import Canvas, Material, ramp

SKIN = Material(ramp("#6b3b2a", "#a8644a", "#d9926b", "#f2bb92", "#ffe0c0"))
EYE = Material(ramp("#10101c", "#1c1c30"), shade=False, outline=False)
WHITE = Material(ramp("#9aa0b8", "#d8dcec", "#ffffff"), shade=False, outline=False)
LEATHER = Material(ramp("#2a160e", "#4a2a18", "#6e4026", "#94603a"))
STEEL = Material(ramp("#3a4058", "#6a7290", "#a4acc8", "#dce2f4", "#ffffff"))
GOLD = Material(ramp("#5a3a08", "#9a6a10", "#d8a428", "#f8dc70"))
WOOD = Material(ramp("#3a2210", "#64401e", "#8c5e30", "#b88450"))


class Hero:
    """How one person looks: materials for each part and a few style choices."""
    def __init__(self, hair, hair_style, tunic, pants, boots=LEATHER, cape=None, hat=None,
                 weapon="sword", trim=GOLD, robe=False):
        self.hair, self.hair_style, self.tunic, self.pants = hair, hair_style, tunic, pants
        self.boots, self.cape, self.hat, self.weapon, self.trim, self.robe = boots, cape, hat, weapon, trim, robe


def mat(*hexes, **kw):
    return Material(ramp(*hexes), **kw)


KAEL = Hero(hair=mat("#5a3a08", "#a07018", "#e0b030", "#fff070"), hair_style="spiky",
            tunic=mat("#14205a", "#24409a", "#3a66d0", "#6a98f0"), pants=mat("#22223a", "#3a3a5a", "#5a5a80"),
            cape=mat("#4a0c14", "#86182a", "#c02a3c", "#e85a60"), weapon="sword")
LYRA = Hero(hair=mat("#5a1440", "#a02a6e", "#e04c9a", "#ff8cc8"), hair_style="long",
            tunic=mat("#2a1050", "#4a2088", "#7034c0", "#9c60e8"), pants=mat("#2a1050", "#4a2088", "#7034c0"),
            hat=mat("#2a1050", "#4a2088", "#7034c0", "#9c60e8"), weapon="rod", robe=True)
BRAM = Hero(hair=mat("#2a1608", "#4e2c12", "#764420", "#9a6430"), hair_style="short",
            tunic=mat("#6a6a80", "#a8acc4", "#dcdeee", "#ffffff"), pants=mat("#3a3a50", "#5a5a78", "#8a8aa8"),
            trim=mat("#5a3a08", "#9a6a10", "#d8a428", "#f8dc70"), weapon="staff", robe=True)

# Villagers for the map.
ELDER = Hero(hair=mat("#6a6a78", "#a8a8b8", "#dcdce8", "#ffffff"), hair_style="short",
             tunic=mat("#1e3a1e", "#2e5a2e", "#448a44", "#6cb46c"), pants=mat("#1e3a1e", "#2e5a2e", "#448a44"), robe=True)
INNKEEPER = Hero(hair=mat("#3a1608", "#6a2a10", "#9a4418", "#c86a30"), hair_style="long",
                 tunic=mat("#5a1414", "#902828", "#c84444", "#ec7a6a"), pants=mat("#3a2a1a", "#5a4430", "#806448"))
SMITH = Hero(hair=mat("#141414", "#2a2a2a", "#444444", "#666666"), hair_style="short",
             tunic=mat("#3a2210", "#64401e", "#8c5e30", "#b88450"), pants=mat("#22223a", "#3a3a5a", "#5a5a80"))
CHILD = Hero(hair=mat("#5a3a08", "#a07018", "#e0b030", "#fff070"), hair_style="short",
             tunic=mat("#0e4a4a", "#1a7a7a", "#2aa8a0", "#5ad4c8"), pants=mat("#3a2a1a", "#5a4430", "#806448"))


# ---- Battle figures (24x32, facing right) -------------------------------------

POSES = {
    #          lean  front-arm back-arm  stride  weapon  head-y  eyes
    "idle":   (0,    20,       -10,      2,      -30,    0,      "open"),
    "ready":  (1,    40,       -20,      3,      20,     0,      "open"),
    "attack": (3,    100,      -40,      5,      95,     1,      "open"),
    "cast":   (0,    150,      120,      2,      160,    0,      "open"),
    "hurt":   (-2,   -30,      -40,      1,      -60,    1,      "shut"),
}


def battler(h, pose_name):
    c = Canvas(24, 32)
    lean, front, back, stride, weapon_angle, head_dy, eyes = POSES[pose_name]
    hip_x, hip_y = 11 + lean * 0.5, 21
    sh_x, sh_y = 11 + lean, 14  # shoulders

    if h.cape:  # flows behind the body
        c.poly([(sh_x - 3, sh_y - 1), (sh_x + 2, sh_y - 1), (hip_x - 1, 29), (hip_x - 8 - lean, 28)], h.cape)
    c.limb(sh_x - 1, sh_y + 1, back, 7, 2.6, h.tunic, -0.6)                       # back arm
    bx, by = c.limb(hip_x - 1, hip_y, -stride * 4, 9, 3, h.pants, -0.5)           # back leg
    c.rect(bx - 2, by - 1, bx + 2, by + 2, h.boots)
    fx, fy = c.limb(hip_x + 1, hip_y, stride * 4, 9, 3, h.pants)                  # front leg
    c.rect(fx - 2, fy - 1, fx + 3, fy + 2, h.boots)
    if h.robe:  # a long robe hides the legs' upper half
        c.poly([(sh_x - 4, sh_y), (sh_x + 4, sh_y), (hip_x + 6, 27), (hip_x - 6, 27)], h.tunic)
        c.rect(hip_x - 6, 26, hip_x + 6, 27, h.trim)
    else:
        c.poly([(sh_x - 4, sh_y), (sh_x + 4, sh_y), (hip_x + 4, hip_y + 1), (hip_x - 4, hip_y + 1)], h.tunic)
        c.rect(hip_x - 4, hip_y - 1, hip_x + 5, hip_y + 1, LEATHER)                # belt
        c.set(hip_x + 1, hip_y, GOLD)
    c.rect(sh_x - 2, sh_y - 2, sh_x + 2, sh_y, SKIN)                              # neck

    hx, hy = sh_x + 1, 8 + head_dy                                                # head
    c.ellipse(hx, hy, 5, 5.5, SKIN)
    hair(c, h, hx, hy)
    face(c, h, hx, hy, eyes == "open")

    ax, ay = c.limb(sh_x + 1, sh_y + 1, front, 7, 2.6, h.tunic)                  # front arm
    c.ellipse(ax, ay, 1.4, 1.4, SKIN)                                             # hand
    weapon(c, h.weapon, ax, ay, weapon_angle)
    return c


def face(c, h, hx, hy, open_eyes):
    """The face in profile, drawn over the hair so fringes don't hide it."""
    c.ellipse(hx + 1.6, hy + 1.2, 3.6, 4.0, SKIN)
    c.set(hx + 4.6, hy + 1, SKIN, 0.4)                                             # nose tip
    if open_eyes:
        c.set(hx + 2, hy, WHITE)
        c.set(hx + 3, hy, EYE)
        c.set(hx + 3, hy - 1, EYE)
        c.set(hx + 2, hy - 2, h.hair)                                              # brow
        c.set(hx + 3, hy - 2, h.hair)
    else:
        c.set(hx + 2, hy, EYE)
        c.set(hx + 3, hy, EYE)
    c.set(hx + 3, hy + 3, SKIN, -0.9)                                              # mouth


def hair(c, h, hx, hy):
    if h.hat:  # a pointed mage hat
        c.poly([(hx - 7, hy - 1), (hx + 6, hy - 1), (hx + 1, hy - 5), (hx - 5, hy - 13), (hx - 2, hy - 5)], h.hat)
        c.rect(hx - 7, hy - 2, hx + 7, hy, h.hat, 0.4)
        c.rect(hx - 6, hy - 1, hx - 1, hy + 9, h.hair)                           # hair down the back
        return
    if h.hair_style == "spiky":
        c.ellipse(hx - 1, hy - 2, 5.5, 4, h.hair)
        for sx, sy in ((-6, -2), (-5, -6), (-1, -7), (3, -6), (5, -3)):
            c.poly([(hx + sx - 1.5, hy + sy + 3), (hx + sx + 1.5, hy + sy + 3), (hx + sx - 2, hy + sy - 1)], h.hair)
        c.rect(hx - 6, hy - 1, hx - 2, hy + 4, h.hair)
    elif h.hair_style == "long":
        c.ellipse(hx - 1, hy - 2, 5.5, 4.5, h.hair)
        c.rect(hx - 6, hy - 2, hx - 1, hy + 9, h.hair)
    else:
        c.ellipse(hx - 1, hy - 2, 5.5, 4, h.hair)
        c.rect(hx - 6, hy - 1, hx - 2, hy + 3, h.hair)


def weapon(c, kind, x, y, angle):
    if kind == "sword":
        c.limb(x, y, angle + 180, 2, 1, LEATHER)                                 # grip
        c.limb(x, y, angle - 90, 2.5, 1.5, GOLD)                                 # guard
        c.limb(x, y, angle + 90, 2.5, 1.5, GOLD)
        c.limb(x, y, angle, 11, 1.8, STEEL)
    elif kind == "rod":
        tx, ty = c.limb(x, y, angle, 10, 1.4, WOOD)
        c.ellipse(tx, ty, 2.2, 2.2, Material(ramp("#5a0a2a", "#c01a5a", "#ff4a8a", "#ffc0e0")))
    elif kind == "staff":
        c.limb(x, y, angle + 180, 6, 1.4, WOOD)
        tx, ty = c.limb(x, y, angle, 9, 1.4, WOOD)
        c.ellipse(tx, ty, 2.4, 2.4, GOLD)


def knocked_out(h):
    """Lying on the ground: the hurt pose turned on its side, at the bottom of the frame."""
    lying = battler(h, "hurt").rotated()   # 32 x 24
    c = Canvas(24, 32)
    out = Canvas(32, 32)
    out.blit(lying, 0, 8)
    return out


# ---- Map figures (16x24 chibis) --------------------------------------------------

def walker(h, facing, frame):
    """facing: down, up or side (right). frame 0/1 are the stride's two phases."""
    c = Canvas(16, 24)
    step = 1 if frame else -1
    cx = 8
    # legs and boots
    for side, phase in ((-1, step), (1, -step)):
        lx = cx + side * 2 + (phase if facing == "side" else 0)
        lift = max(0, phase) if facing != "side" else 0
        c.rect(lx - 1.5, 17, lx + 1.5, 22 - lift, h.pants)
        c.rect(lx - 1.5, 21 - lift, lx + 1.5, 23 - lift, h.boots)
    # body
    if h.robe:
        c.poly([(cx - 4, 12), (cx + 4, 12), (cx + 5, 21), (cx - 5, 21)], h.tunic)
    else:
        c.rect(cx - 4, 12, cx + 4, 19, h.tunic)
        c.rect(cx - 4, 17, cx + 4, 18, LEATHER)
    if h.cape and facing != "down":
        c.rect(cx - 4, 12, cx + 4, 20, h.cape, -0.2)
    # arms swing with the stride
    for side in (-1, 1):
        if facing == "side" and side == -1:
            continue
        sway = side * step if facing != "side" else step
        c.rect(cx + side * 5 - 1, 12 + max(0, sway), cx + side * 5 + 1, 17 + max(0, sway), h.tunic, -0.3)
        c.set(cx + side * 5, 17 + max(0, sway), SKIN)
    # head
    c.ellipse(cx, 7, 5.5, 5.5, SKIN)
    if facing == "up":
        c.ellipse(cx, 6, 5.8, 5.5, h.hair)
    else:
        c.ellipse(cx, 4.5, 5.8, 3.6, h.hair)
        if facing == "down":
            c.rect(cx - 6, 4, cx - 4, 9, h.hair)
            c.rect(cx + 4, 4, cx + 6, 9, h.hair)
            c.set(cx - 2, 8, EYE); c.set(cx + 2, 8, EYE)
            c.set(cx - 2, 7, EYE); c.set(cx + 2, 7, EYE)
        else:
            c.rect(cx - 6, 4, cx - 1, 10, h.hair)
            c.set(cx + 3, 8, EYE); c.set(cx + 3, 7, EYE)
    if h.hat:
        c.poly([(cx - 7, 4), (cx + 7, 4), (cx + 1, -1), (cx - 1, -1)], h.hat)
        c.rect(cx - 7, 3, cx + 7, 5, h.hat, 0.3)
    if h.hair_style == "spiky" and not h.hat:
        for sx in (-4, -1, 2, 5):
            c.poly([(cx + sx - 1.5, 3), (cx + sx + 1.5, 3), (cx + sx, -0.5)], h.hair)
    return c
