#!/usr/bin/env python3
"""Generates Embers of Aldane's pixel art (one PNG per frame + the atlas
config), sound effects and music. All original. Re-run after editing:
python3 tools/gen_assets.py  (needs numpy and Pillow)."""
import json
import math
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
SR = 22050

# ---- Art ------------------------------------------------------------------------

PALETTE = {
    "K": (22, 20, 30), "W": (246, 246, 236), "S": (250, 200, 156), "H": (110, 66, 36),
    "B": (60, 100, 210), "U": (130, 180, 250), "R": (200, 56, 56), "D": (120, 30, 40),
    "P": (226, 110, 170), "Q": (150, 60, 120), "Y": (250, 214, 80), "O": (232, 132, 44),
    "G": (68, 150, 76), "L": (130, 210, 106), "T": (38, 96, 50), "N": (110, 72, 40),
    "M": (160, 120, 80), "E": (172, 178, 192), "F": (104, 108, 124), "A": (60, 60, 92),
    "C": (220, 220, 240), "V": (110, 70, 170), "X": (0, 0, 0), "Z": (255, 246, 200),
    "J": (90, 200, 120), "I": (200, 240, 255), "Ö": (255, 160, 60),
}
sources = []


def save(name, rows, scale=1):
    h, w = len(rows), max(len(r) for r in rows)
    img = Image.new("RGBA", (w, h))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), PALETTE[ch] + (255,))
    if scale != 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    store(name, img)


def store(name, img):
    path = os.path.join(ROOT, "assets", "textures", name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)
    sources.append("assets/textures/" + name + ".png")


def grid(fn, w=16, h=16):
    return ["".join(fn(x, y) for x in range(w)) for y in range(h)]


def speckle(x, y, every=17):
    return (x * 37 + y * 101 + x * y * 13) % every == 0


# Battle sprites face left (heroes stand on the right). 16x24.
def hero(hair, cloth, trim, weapon):
    head = [
        "......hhhh......", ".....hhhhhh.....", "....hhhhhhhh....", "....hhSSSShh....",
        "....hSKSSKSh....", ".....SSSSSS.....", "......SSSS......",
    ]
    body = [
        ".....cccccc.....", "....cccttccc....", "...Sccctttcc....", "...Scccttcccw...",
        "....cccccccc.w..", "....cccccccc.w..", ".....cccccc..w..", ".....cc..cc.....",
        ".....cc..cc.....", ".....NN..NN.....", "....NNN..NNN....",
    ]
    rows = ["................"] * 6 + head + body
    return [r.replace("h", hair).replace("c", cloth).replace("t", trim).replace("w", weapon) for r in rows]


def pose(rows, kind):
    if kind == "attack":  # lean in, weapon forward
        return [r[1:] + "." for r in rows]
    if kind == "hurt":
        return [r if i < 6 else "." + r[:-1] for i, r in enumerate(rows)]
    if kind == "ko":  # lying down: rotate the sprite a quarter turn
        img = ["".join(r[x] for r in rows) for x in range(16)]
        return ["." * 24] * 8 + [("." * 4 + r[::-1])[:24] for r in img[::-1]][:16]
    return rows


HEROES = {"kael": hero("Y", "B", "U", "E"), "lyra": hero("P", "V", "P", "N"), "bram": hero("H", "C", "Y", "N")}


def walker(hair, cloth):
    """Map sprites, 16x16: down/up/side, two frames."""
    down = [
        "................", ".....hhhhhh.....", "....hhhhhhhh....", "....hSSSSSSh....",
        "....hSKSSKSh....", ".....SSSSSS.....", "....cccccccc....", "...Scccccccc S..".replace(" ", "c"),
        "...Scccccccc S..".replace(" ", "c"), "....cccccccc....", ".....cc..cc.....", ".....NN..NN.....",
        "....NNN..NNN....", "................", "................", "................",
    ]
    up = [r.replace("S", "h").replace("K", "h") if i < 6 else r for i, r in enumerate(down)]
    side = [
        "................", ".....hhhhhh.....", "....hhhhhhh.....", "....hhhSSSS.....",
        "....hhSSSKS.....", ".....SSSSSS.....", ".....cccccc.....", "....ccccccS.....",
        "....ccccccS.....", ".....cccccc.....", ".....cc.cc......", "....NN...NN.....",
        "...NNN...NNN....", "................", "................", "................",
    ]
    out = {}
    for name, rows in (("down", down), ("up", up), ("side", side)):
        rows = [r.replace("h", hair).replace("c", cloth) for r in rows]
        out[name + "_0"] = rows
        out[name + "_1"] = rows[:-5] + [r[1:] + "." if i % 2 else "." + r[:-1] for i, r in enumerate(rows[-5:])]
    return out


def blob(color, light, size=24):
    def px(x, y):
        dx, dy = (x - size / 2 + 0.5) / (size / 2), (y - size + 1) / (size * 0.62)
        if dx * dx + dy * dy > 1 or y < size * 0.3:
            return "."
        if (x - size * 0.35) ** 2 + (y - size * 0.55) ** 2 < 6:
            return light
        if abs(y - size * 0.62) < 1.5 and abs(abs(x - size / 2 + 0.5) - 3) < 1.2:
            return "K"
        return color
    return grid(px, size, size)


GOBLIN = [
    "........................", "........................", "........JJJJJJ..........",
    ".....J.JJJJJJJJ.J.......", ".....JJJJWKJJWKJJ.......", "......JJJJJJJJJJ........",
    ".......JJJKKKJJ.........", "........JJJJJJ..........", "......NNNNNNNNNN........",
    ".....NNNNMMNNNNNN.......", "....JNNNNMMNNNNNJ.......", "....JJNNNNNNNNNJJE......",
    "......NNNNNNNNN...E.....", "......NNNNNNNNN....E....", ".......NNN..NNN.........",
    ".......NN....NN.........", ".......JJ....JJ.........", "......JJJ....JJJ........",
] + ["........................"] * 6


def wisp(frame):
    def px(x, y):
        dx = x - 11.5 + math.sin(y * 0.6 + frame) * 1.5
        r = (24 - y) * 0.42
        if y < 3 or abs(dx) > r:
            return "."
        if abs(dx) < r * 0.35 and y > 10:
            return "W"
        if abs(dx) < r * 0.7:
            return "I"
        return "U"
    rows = grid(px, 24, 24)
    rows[14] = rows[14][:8] + "KK" + rows[14][10:13] + "KK" + rows[14][15:]
    return rows


def wyrm(frame):
    """The Cinder Wyrm, 64x48, facing right."""
    def px(x, y):
        bx, by = x - 26, y - 30
        body = (bx / 20) ** 2 + (by / 11) ** 2 < 1
        neck = abs(y - (28 - (x - 38) * 0.9)) < 4.5 and 36 < x < 52
        head = (x - 54) ** 2 / 36 + (y - 13 - frame) ** 2 / 16 < 1
        wing = y < 26 and x > 10 and x < 40 and (x - 10) * 0.9 > (26 - y) * 1.2 - frame * 2 and y > 6 + abs(x - 25) * 0.3
        tail = abs(y - (34 + (x - 8) * 0.25)) < 3 - (8 - x) * 0.2 and x < 10 and x > 0
        legs = (abs(x - 18) < 3 or abs(x - 34) < 3) and 38 < y < 47
        if head and abs(x - 56) < 1.2 and abs(y - 11 - frame) < 1.2:
            return "Y"
        if wing:
            return "D" if (x + y) % 6 else "Q"
        if body or neck or head or tail or legs:
            if by > 4 and body:
                return "Ö"
            return "R" if not speckle(x, y, 9) else "D"
        return "."
    return grid(px, 64, 48)


def effect_frames():
    out = {}
    for i in range(3):
        r = 4 + i * 3
        out[f"slash_{i}"] = grid(lambda x, y: "W" if abs(((x - 2) ** 2 + (y - 14) ** 2) ** 0.5 - r * 1.2) < 1.3 and x > y - 2 else ".")
        out[f"fire_{i}"] = grid(lambda x, y: ("Y" if ((x - 7.5) ** 2 + (y - 9) ** 2) ** 0.5 < r * 0.5 else "O") if ((x - 7.5) ** 2 + (y - 9 + i) ** 2) ** 0.5 < r * 0.9 and y > 14 - r * 1.6 else ".")
        out[f"ice_{i}"] = grid(lambda x, y: ("W" if abs(x - 7.5) < 1 else "I") if abs(x - 7.5) + abs(y - 8) * 0.6 < r * 0.8 and abs(x - 7.5) < r * 0.5 + 1 else ".")
        out[f"bolt_{i}"] = grid(lambda x, y: "Y" if abs(x - (7 + ((y // 3) % 2) * (3 - i))) < 1.5 and y < 4 + r * 1.2 else ".")
        out[f"heal_{i}"] = grid(lambda x, y: "L" if (x + y * 3 + i * 5) % 11 == 0 and abs(x - 7.5) < 6 and y > 12 - r else ".")
    return out


GRASS = grid(lambda x, y: "L" if speckle(x, y) else "G")
TALL = grid(lambda x, y: "L" if (x % 4 == 1 and y % 5 < 3) else ("T" if speckle(x, y, 7) else "G"))
PATH = grid(lambda x, y: "N" if speckle(x, y, 13) else "M")
WATER = grid(lambda x, y: "U" if (y % 6 == 0 and x % 8 < 4) or (y % 6 == 3 and (x + 4) % 8 < 4) else "B")
TREE = grid(lambda x, y: ("N" if y > 11 and 6 < x < 10 else ("L" if speckle(x, y) else "G")) if (x - 7.5) ** 2 + (y - 6) ** 2 >= 42 else
            ("L" if (x - 5) ** 2 + (y - 4) ** 2 < 6 else ("T" if speckle(x, y, 5) else "G")))
ROOF = grid(lambda x, y: "D" if y % 4 == 3 else ("R" if (x + (y // 4) * 2) % 8 else "D"))
WALL = grid(lambda x, y: "N" if y % 8 == 7 or x % 16 == 15 else ("M" if y % 8 else "N"))
DOOR = grid(lambda x, y: ("Y" if x == 11 and y == 9 else "N" if 3 < x < 12 and y > 2 else "M") if True else ".")
WINDOW = grid(lambda x, y: ("U" if 4 < x < 11 and 4 < y < 11 and x != 7 and y != 7 else "K") if 3 < x < 12 and 3 < y < 12 else ("N" if y % 8 == 7 else "M"))
FENCE = grid(lambda x, y: "N" if (y in (6, 7, 11, 12)) or x in (2, 3, 12, 13) and y > 3 else ("L" if speckle(x, y) else "G"))
FLOWERS = grid(lambda x, y: "P" if (x, y) in ((3, 4), (11, 9), (6, 12)) else "Y" if (x, y) in ((4, 4), (12, 9), (7, 12)) else ("L" if speckle(x, y) else "G"))
CHEST = grid(lambda x, y: ("Y" if (x in (7, 8) and 6 < y < 10) else ("N" if y in (7,) or x in (2, 13) else "O")) if 2 <= x <= 13 and 4 <= y <= 13 else ".")
CHEST_OPEN = grid(lambda x, y: ("K" if 3 < x < 12 and 4 < y < 8 else ("N" if x in (2, 13) or y in (8,) else "O")) if 2 <= x <= 13 and 4 <= y <= 13 else ".")
CRYSTAL = grid(lambda x, y: ("W" if x < 8 and y < 8 else "I" if x < 8 else "U") if abs(x - 7.5) * 1.5 + abs(y - 7.5) < 8 else ".")
CAVE = grid(lambda x, y: "K" if (x - 7.5) ** 2 / 30 + (y - 16) ** 2 / 100 < 1 else ("F" if speckle(x, y, 5) else "E"))
ROCK = grid(lambda x, y: ("E" if (x - 6) ** 2 + (y - 6) ** 2 < 14 else "F") if (x - 7.5) ** 2 + (y - 8) ** 2 < 50 else ("L" if speckle(x, y) else "G"))
BRIDGE = grid(lambda x, y: "N" if y % 4 == 0 else "M")


def npc(robe, hair):
    rows = walker(hair, robe)["down_0"]
    return rows


def backdrop(kind):
    w, h = 320, 168
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            if kind == "forest":
                if y < 70:
                    t = y / 70
                    c = (int(90 + 60 * t), int(140 + 50 * t), int(210 - 20 * t))
                    far = 50 + 10 * math.sin(x * 0.07) + 6 * math.sin(x * 0.19)
                    if y > far:
                        c = (40, 90, 60) if (x * 3 + y) % 9 else (30, 70, 48)
                elif y < 100:
                    c = (52, 120, 64) if (x // 6 + y // 5) % 3 else (40, 100, 54)
                else:
                    c = (78, 150, 74) if not speckle(x, y, 11) else (110, 190, 98)
            else:  # lair
                glow = max(0, 1 - abs(y - 120) / 60)
                c = (int(40 + 80 * glow), int(18 + 20 * glow), int(26 + 10 * glow))
                if y > 120 and (x * 7 + y * 3) % 23 == 0:
                    c = (220, 120, 40)
                if y < 40 and (x + y * 2) % 37 < 3:
                    c = (60, 40, 50)
            px[x, y] = c + (255,)
    store("bg_" + kind, img)


def art():
    for name, rows in HEROES.items():
        for kind in ("idle", "attack", "hurt", "ko"):
            save(f"{name}_{kind}", pose(rows, kind))
    for name, rows in walker("Y", "B").items():
        save(f"walk_{name}", rows)
    save("jelly", blob("J", "L"))
    save("jelly_squish", blob("J", "L")[2:] + ["." * 24] * 2)
    save("goblin", GOBLIN)
    save("wisp_0", wisp(0))
    save("wisp_1", wisp(2))
    save("wyrm_0", wyrm(0))
    save("wyrm_1", wyrm(1))
    for name, rows in effect_frames().items():
        save(name, rows)
    for name, rows in (("grass", GRASS), ("tall_grass", TALL), ("path", PATH), ("water", WATER), ("tree", TREE),
                       ("roof", ROOF), ("wall", WALL), ("door", DOOR), ("window", WINDOW), ("fence", FENCE),
                       ("flowers", FLOWERS), ("chest", CHEST), ("chest_open", CHEST_OPEN), ("crystal", CRYSTAL),
                       ("cave", CAVE), ("rock", ROCK), ("bridge", BRIDGE)):
        save(name, rows)
    save("npc_elder", npc("V", "E"))
    save("npc_inn", npc("R", "H"))
    save("npc_child", npc("P", "Y"))
    save("npc_smith", npc("N", "K"))
    backdrop("forest")
    backdrop("lair")
    path = os.path.join(ROOT, "assets", "atlases", "sprites.atlas.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump({"sources": sources, "filter": "nearest", "padding": 2}, f, indent=2)


# ---- Audio ------------------------------------------------------------------------

def t(seconds):
    return np.arange(int(seconds * SR)) / SR


def hz(note):
    if note in (None, "-"):
        return 0.0
    semis = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[note[0]]
    rest = note[1:]
    if rest[0] == "#":
        semis, rest = semis + 1, rest[1:]
    elif rest[0] == "b":
        semis, rest = semis - 1, rest[1:]
    return 440.0 * 2 ** ((12 * (int(rest) + 1) + semis - 69) / 12)


def square(freq, seconds, duty=0.5, decay=0.0):
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


def noise(seconds, decay):
    x = t(seconds)
    return np.random.default_rng(11).uniform(-1, 1, len(x)) * np.exp(-decay * x)


def notes(seq, step, duty=0.5, decay=4.0):
    return np.concatenate([square(hz(n), step, duty, decay) for n in seq])


def write(name, samples, gain=0.8, loop=False):
    peak = np.max(np.abs(samples)) or 1.0
    data = np.clip(samples / peak * gain, -1, 1)
    if not loop:
        k = min(len(data), int(0.006 * SR))
        data[len(data) - k:] *= np.linspace(1, 0, k)
    path = os.path.join(ROOT, "assets", "sounds", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((data * 32767).astype("<i2").tobytes())


def sfx():
    write("cursor.wav", square(hz("E6"), 0.03, 0.5, 40), 0.35)
    write("confirm.wav", notes(["A5", "E6"], 0.04, 0.5, 10), 0.4)
    write("cancel.wav", notes(["E5", "A4"], 0.04, 0.5, 10), 0.4)
    write("hit.wav", noise(0.12, 25) + 0.5 * square(hz("C3"), 0.12, 0.5, 25), 0.6)
    write("critical.wav", noise(0.2, 15) + 0.6 * sweep(900, 200, 0.2), 0.7)
    write("fire.wav", noise(0.5, 4) * np.sin(np.linspace(0, np.pi, int(0.5 * SR))) + 0.3 * sweep(200, 600, 0.5), 0.6)
    write("ice.wav", notes(["E7", "B6", "G7", "D7", "A7"], 0.05, 0.5, 8), 0.4)
    write("bolt.wav", noise(0.4, 6) * (np.sin(t(0.4) * 90) > 0) + 0.4 * sweep(1500, 100, 0.4, 0.25), 0.6)
    write("heal.wav", notes(["C5", "E5", "G5", "C6", "E6", "G6"], 0.06, 0.25, 4), 0.45)
    write("status.wav", notes(["G4", "F#4", "F4", "E4"], 0.08, 0.25, 4), 0.45)
    write("defeat.wav", sweep(600, 60, 0.45) * np.exp(-4 * t(0.45)) + 0.4 * noise(0.45, 6), 0.6)
    write("miss.wav", sweep(800, 1200, 0.1, 0.25) * np.exp(-20 * t(0.1)), 0.35)
    write("encounter.wav", np.concatenate([sweep(200, 1600, 0.35, 0.25), sweep(1600, 200, 0.35, 0.25)]), 0.5)
    write("level_up.wav", notes(["C5", "E5", "G5", "C6", "-", "G5", "C6"], 0.09, 0.5, 2), 0.5)
    write("victory.wav", notes(["C5", "C5", "C5", "C5", "-", "G#4", "-", "A#4", "-", "C5", "-", "A#4", "C5"], 0.11, 0.5, 2), 0.5)
    write("chest.wav", notes(["G4", "C5", "E5", "G5", "C6"], 0.06, 0.5, 4), 0.45)
    write("inn.wav", notes(["C5", "G4", "E5", "C5", "G5", "-", "C6"], 0.18, 0.5, 1.5), 0.45)
    write("save.wav", notes(["A5", "C#6", "E6", "A6"], 0.08, 0.5, 3), 0.45)
    write("text.wav", square(hz("A5"), 0.02, 0.5, 60), 0.22)
    write("game_over.wav", notes(["A4", "-", "E4", "-", "C4", "D4", "E4", "A3"], 0.2, 0.5, 1.5), 0.5)


def song(melody, bass, beat, name, duty=0.5):
    lead = np.concatenate([np.concatenate([square(hz(n), beat * b - 0.015, duty, 2.0), np.zeros(int(0.015 * SR))]) for n, b in melody])
    low = np.concatenate([tri(hz(n), beat * b) for n, b in bass])
    n = min(len(lead), len(low))
    write(name, 0.5 * lead[:n] + 0.5 * low[:n], 0.6, loop=True)


def music():
    # Aldane village: warm and slow, F major.
    town = [("A4", 1), ("C5", 1), ("F5", 2), ("E5", 1), ("D5", 1), ("C5", 2), ("Bb4", 1), ("A4", 1), ("G4", 2), ("A4", 4),
            ("A4", 1), ("C5", 1), ("F5", 2), ("G5", 1), ("A5", 1), ("G5", 2), ("F5", 1), ("E5", 1), ("C5", 2), ("F5", 4)]
    town_bass = [(n, 2) for n in ["F3", "C3", "F3", "C3", "Bb2", "F3", "C3", "F3", "F3", "C3", "D3", "A2", "Bb2", "C3", "F3", "C3"]]
    song(town, town_bass, 0.3, "music_town.wav", 0.25)
    # Emberwood field: an adventurous D minor theme.
    field = [("D5", 1), ("A4", 0.5), ("D5", 0.5), ("F5", 1), ("E5", 1), ("D5", 1.5), ("C5", 0.5), ("A4", 2),
             ("Bb4", 1), ("C5", 1), ("D5", 1), ("F5", 1), ("E5", 2), ("A4", 2),
             ("D5", 1), ("F5", 1), ("A5", 1.5), ("G5", 0.5), ("F5", 1), ("E5", 1), ("D5", 2),
             ("C5", 1), ("E5", 1), ("D5", 4)]
    field_bass = [(n, 1) for n in ["D3", "A3", "D3", "A3", "D3", "A3", "F3", "A3", "Bb2", "F3", "Bb2", "F3", "A2", "E3", "A2", "E3",
                                    "D3", "A3", "D3", "A3", "Bb2", "F3", "G2", "D3", "C3", "G3", "A2", "E3", "D3", "A2", "D3", "A2"]]
    song(field, field_bass, 0.22, "music_field.wav")
    # Battle: driving A minor.
    battle = [("A5", 0.5), ("E5", 0.5), ("A5", 0.5), ("C6", 0.5), ("B5", 0.5), ("A5", 0.5), ("G5", 0.5), ("E5", 0.5)] * 2 + \
             [("F5", 0.5), ("C5", 0.5), ("F5", 0.5), ("A5", 0.5), ("G#5", 0.5), ("E5", 0.5), ("B4", 0.5), ("E5", 0.5)] * 2
    battle_bass = [("A2", 0.5), ("A3", 0.5)] * 8 + [("F2", 0.5), ("F3", 0.5)] * 4 + [("E2", 0.5), ("E3", 0.5)] * 4
    song(battle, battle_bass, 0.17, "music_battle.wav", 0.25)
    # The wyrm: heavy and chromatic.
    boss = [("E5", 0.5), ("F5", 0.5), ("E5", 0.5), ("D#5", 0.5), ("E5", 1), ("B4", 1),
            ("C5", 0.5), ("D5", 0.5), ("C5", 0.5), ("B4", 0.5), ("A4", 2)] * 2
    boss_bass = [("E2", 0.5), ("E2", 0.5), ("E3", 0.5), ("E2", 0.5)] * 4 + [("A1", 0.5), ("A2", 0.5)] * 8
    song(boss, boss_bass, 0.19, "music_boss.wav", 0.25)


if __name__ == "__main__":
    art()
    sfx()
    music()
    print("assets written")
