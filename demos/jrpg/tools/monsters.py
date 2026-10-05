"""Enemies, facing right, drawn at their native battle size."""
import math

from characters import EYE, WHITE
from pixelart import Canvas, Material, ramp

JELLY = Material(ramp("#0e4a2a", "#1a7a3e", "#2ea850", "#5ad070", "#a8f0b0"))
SHINE = Material(ramp("#d8ffe0", "#ffffff"), shade=False, outline=False)


def jelly(frame):
    """A wobbling slime, 32x24. Frame 1 is squashed."""
    c = Canvas(32, 24)
    rx, ry = (12, 9) if frame == 0 else (13.5, 7.5)
    c.ellipse(16, 23 - ry, rx, ry, JELLY)
    c.rect(16 - rx + 1, 21, 16 + rx - 1, 23, JELLY, -0.4)
    c.ellipse(11, 23 - ry * 1.5, 3, 2, SHINE)
    for ex in (19, 24):
        c.ellipse(ex, 23 - ry, 1.6, 2.2, EYE)
        c.set(ex, 22 - ry, WHITE)
    return c


GOBLIN_SKIN = Material(ramp("#1a3a12", "#2e5e1e", "#4a8a2c", "#6eb43c", "#9ad860"))
CLOTH = Material(ramp("#3a0e0e", "#6a1a16", "#9a2a20", "#c44a30"))
CLUB = Material(ramp("#2a160a", "#4a2c14", "#6e4420", "#8e5c30"))
RED_EYE = Material(ramp("#ff3a20", "#ffd040"), shade=False, outline=False)


def goblin(frame):
    """A hunched goblin with a club, 32x32."""
    c = Canvas(32, 32)
    bob = frame
    c.limb(13, 21 + bob, -15, 8, 3.2, GOBLIN_SKIN, -0.5)        # legs
    c.limb(17, 21 + bob, 20, 8, 3.2, GOBLIN_SKIN)
    c.rect(9, 28, 14, 31, GOBLIN_SKIN, -0.6)
    c.rect(16, 28, 22, 31, GOBLIN_SKIN)
    c.ellipse(15, 18 + bob, 6.5, 6, GOBLIN_SKIN)                 # belly
    c.rect(9, 19 + bob, 21, 23 + bob, CLOTH)                     # loincloth
    c.ellipse(17, 10 + bob, 6.5, 5.5, GOBLIN_SKIN)               # head
    c.poly([(11, 8 + bob), (5, 4 + bob), (12, 11 + bob)], GOBLIN_SKIN, 0.4)   # ear
    c.set(20, 9 + bob, RED_EYE)
    c.set(21, 9 + bob, RED_EYE)
    c.rect(19, 13 + bob, 23, 14 + bob, Material(ramp("#0e1a08", "#1a2a10"), shade=False))  # grin
    c.set(21, 13 + bob, WHITE)
    ax, ay = c.limb(19, 16 + bob, 70 - frame * 30, 6, 2.6, GOBLIN_SKIN)        # arm and club
    tx, ty = c.limb(ax, ay, 150 - frame * 40, 9, 2.4, CLUB)
    c.ellipse(tx, ty, 3, 3, CLUB)
    return c


FLAME_OUT = Material(ramp("#1a3a8a", "#2a5ad0", "#4a8cf0", "#8ac0ff"), outline=False)
FLAME_IN = Material(ramp("#6aa8ff", "#a8d8ff", "#e0f4ff", "#ffffff"), outline=False)


def wisp(frame):
    """A will-o'-wisp: layered cold flame with a face, 24x32."""
    c = Canvas(24, 32)
    for layer, scale in ((FLAME_OUT, 1.0), (FLAME_IN, 0.6)):
        pts = []
        for i in range(24):
            t = i / 24 * math.pi * 2
            r = (7 + 2.5 * math.sin(t * 3 + frame * 2)) * scale
            y = math.sin(t) * 11 * scale
            if y < 0:
                y *= 1.4 + 0.3 * math.sin(t * 5 + frame)   # licking upward
            pts.append((12 + math.cos(t) * r, 20 + y))
        c.poly(pts, layer)
    c.rect(9, 19, 11, 21, EYE)
    c.rect(14, 19, 16, 21, EYE)
    return c


SCALES = Material(ramp("#2a0606", "#5a0e0e", "#8e1a14", "#c8361e", "#f06a3a"))
BELLY = Material(ramp("#5a3a10", "#9a6a20", "#d8a03a", "#f8d070"))
MEMBRANE = Material(ramp("#2a0612", "#4e0e22", "#7a1a34", "#a83a4c"))
HORN = Material(ramp("#3a3020", "#7a6a48", "#bcae84", "#ece0bc"))
EMBER = Material(ramp("#ffb040", "#fff0a0"), shade=False, outline=False)


def wyrm(frame):
    """The Cinder Wyrm, 112x80, facing right. Frame 1 has its wings down."""
    c = Canvas(112, 80)
    lift = 0 if frame == 0 else 8
    # far wing
    c.poly([(40, 30), (58, 2 + lift), (72, 8 + lift), (66, 30)], MEMBRANE, -0.6)
    # tail
    pts = [(4 + i * 3, 58 - math.sin(i * 0.5) * 6) for i in range(12)]
    for i, (x, y) in enumerate(pts):
        c.ellipse(x, y, 2 + i * 0.45, 2 + i * 0.45, SCALES)
    # body and belly
    c.ellipse(52, 50, 24, 16, SCALES)
    c.ellipse(56, 56, 16, 9, BELLY)
    for i in range(5):
        c.line(44 + i * 6, 50, 44 + i * 6, 64, BELLY, 1, -0.9)   # belly plates
    # legs with claws
    for lx in (40, 64):
        fx, fy = c.limb(lx, 58, 10, 14, 6, SCALES)
        for k in (-2, 0, 2):
            c.limb(fx + k, fy, 120, 3, 1.5, HORN)
    # neck and head
    neck = [(70 + i * 3.4, 44 - i * 4.2) for i in range(8)]
    for i, (x, y) in enumerate(neck):
        c.ellipse(x, y, 6 - i * 0.25, 6 - i * 0.25, SCALES)
        c.set(x + 3, y + 3, BELLY)
    hx, hy = 96, 12 + frame
    c.ellipse(hx, hy, 9, 7, SCALES)
    c.poly([(hx + 4, hy - 2), (hx + 16, hy + 2), (hx + 14, hy + 6), (hx + 2, hy + 6)], SCALES)   # snout
    c.poly([(hx + 2, hy + 6), (hx + 14, hy + 7), (hx + 4, hy + 10)], SCALES, -0.6)                 # jaw
    c.poly([(hx - 4, hy - 5), (hx - 14, hy - 12), (hx - 2, hy - 2)], HORN)
    c.poly([(hx + 1, hy - 6), (hx - 6, hy - 15), (hx + 4, hy - 4)], HORN)
    c.set(hx + 5, hy - 1, EMBER)
    c.set(hx + 6, hy - 1, EMBER)
    c.set(hx + 15, hy + 7, EMBER)
    # near wing with bones
    tip = (30, 4 + lift)
    c.poly([(46, 36), (tip[0], tip[1]), (18, 22 + lift), (24, 34 + lift // 2), (40, 42)], MEMBRANE)
    for bx, by in ((tip[0], tip[1]), (18, 22 + lift), (24, 34 + lift // 2)):
        c.line(46, 36, bx, by, SCALES, 1.6)
    return c
