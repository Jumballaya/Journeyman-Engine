#!/usr/bin/env python3
"""Generates Hollow Grove's pixel art (one PNG per frame + the atlas config),
sound effects and music. All original. Re-run after editing:
python3 tools/gen_assets.py  (needs numpy and Pillow)."""
import json
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
SR = 22050

# ---- Art ----------------------------------------------------------------------

PALETTE = {
    "K": (20, 18, 26), "W": (246, 246, 236), "S": (248, 196, 150), "H": (120, 72, 40),
    "R": (196, 52, 52), "D": (120, 28, 36), "G": (64, 148, 72), "Q": (34, 92, 46),
    "L": (128, 208, 104), "N": (96, 60, 34), "Y": (250, 214, 80), "O": (232, 132, 44),
    "E": (170, 176, 190), "F": (104, 108, 124), "B": (70, 120, 220), "U": (150, 200, 255),
    "P": (150, 90, 200), "V": (90, 50, 140), "T": (222, 196, 140), "M": (172, 146, 98),
    "A": (60, 60, 90), "C": (40, 40, 64), "X": (0, 0, 0), "Z": (255, 250, 200),
}
sources = []


def save(name, rows):
    h, w = len(rows), max(len(r) for r in rows)
    img = Image.new("RGBA", (w, h))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), PALETTE[ch] + (255,))
    path = os.path.join(ROOT, "assets", "textures", name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)
    sources.append("assets/textures/" + name + ".png")


def grid(fn, w=16, h=16):
    return ["".join(fn(x, y) for x in range(w)) for y in range(h)]


def legs(rows, frame):
    """Swaps the stride of the last three rows for the second walk frame."""
    if frame == 0:
        return rows
    return rows[:-3] + [r[1:] + "." if i % 2 == 0 else "." + r[:-1] for i, r in enumerate(rows[-3:])]


# Wren, the hero: a red hood and a green tunic. Side frames face right.
WREN_DOWN = [
    ".....RRRRRR.....", "....RRRRRRRR....", "...RRRHHHHRRR...", "...RRHSSSSHRR...",
    "...RHSKSSKSHR...", "....HSSSSSSH....", ".....SSSSSS.....", "....GGRRRRGG....",
    "...SGGGRRGGGS...", "...SSGGGGGGSS...", ".....GGGGGG.....", ".....GGYYGG.....",
    ".....NN..NN.....", ".....NN..NN.....", "....NNN..NNN....", "................",
]
WREN_UP = [
    ".....RRRRRR.....", "....RRRRRRRR....", "...RRRRRRRRRR...", "...RRRRRRRRRR...",
    "...RRRDRRDRRR...", "....RRRRRRRR....", ".....RRRRRR.....", "....GGRRRRGG....",
    "...SGGGGGGGGS...", "...SSGGGGGGSS...", ".....GGGGGG.....", ".....GGYYGG.....",
    ".....NN..NN.....", ".....NN..NN.....", "....NNN..NNN....", "................",
]
WREN_SIDE = [
    ".....RRRRR......", "....RRRRRRR.....", "...RRRRHHHH.....", "...RRRHSSSS.....",
    "...RRRHSSKS.....", "....RRHSSSSS....", ".....RSSSS......", ".....GGGRR......",
    "....GGGGGGS.....", "....GGGGGGS.....", ".....GGGGG......", ".....GGYYG......",
    ".....NN.NN......", "....NN...NN.....", "...NNN...NNN....", "................",
]
SWORD = [  # blade pointing up
    ".......WE.......", ".......WE.......", ".......WE.......", ".......WE.......",
    ".......WE.......", ".......WE.......", ".......WE.......", ".......WE.......",
    ".......WE.......", ".....YYYYYY.....", "......YNNY......", ".......NN.......",
    ".......NN.......", ".......YY.......", "................", "................",
]
SLIME = [
    "................", "................", "................", "................",
    "................", "......BBBB......", "....BBBBBBBB....", "...BBUUBBBBBB...",
    "..BBUUBBBBBBBB..", "..BBBWKBBWKBBB..", ".BBBBWKBBWKBBBB.", ".BBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBB.", "..BBBBBBBBBBBB..", "...AAAAAAAAAA...", "................",
]
SLIME_SQUASH = ["................"] * 7 + [
    "....BBBBBBBB....", "..BBUUBBBBBBBB..", ".BBBBWKBBWKBBBB.", "BBBBBWKBBWKBBBBB",
    "BBBBBBBBBBBBBBBB", "BBBBBBBBBBBBBBBB", ".BBBBBBBBBBBBBB.", ".AAAAAAAAAAAAAA.", "................",
]
BAT_UP = [
    "................", "................", "P..............P", "PP............PP",
    "PPP..........PPP", "PPPP...PP...PPPP", ".PPPPPPPPPPPPPP.", "..PPPPWKKWPPPP..",
    "...PPPPPPPPPP...", "......PVVP......", ".......VV.......", "................",
    "................", "................", "................", "................",
]
BAT_DOWN = ["................"] * 5 + [
    ".......PP.......", "....PPPPPPPP....", "..PPPPWKKWPPPP..", ".PPPPPPPPPPPPPP.",
    "PPPP..PVVP..PPPP", "PPP....VV....PPP", "PP............PP", "P..............P",
    "................", "................", "................",
]
SKULL = [
    "................", ".....WWWWWW.....", "....WWWWWWWW....", "....WKKWWKKW....",
    "....WKKWWKKW....", "....WWWWWWWW....", ".....WKWKWW.....", "......WWWW......",
    "....EEWWWWEE....", "...EE.WWWW.EE...", "...E..WWWW..E...", "......EEEE......",
    "......E..E......", ".....EE..EE.....", ".....E....E.....", "................",
]
SKULL2 = SKULL[:12] + ["......E..E......", "......E..EE.....", ".....EE...E.....", "................"]
POOF = [grid(lambda x, y, r=r: "W" if r - 1.5 < ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5 < r else ".") for r in (3, 5, 7)]
HEART = grid(lambda x, y: "R" if ((x - 4.5) ** 2 + (y - 5) ** 2 < 10 or (x - 10.5) ** 2 + (y - 5) ** 2 < 10 or
                                  (y >= 5 and abs(x - 7.5) < 13 - y)) and 1 < y < 14 else ".")
HEART_HALF = [r[:8] + "".join("F" if c == "R" else c for c in r[8:]) for r in HEART]
HEART_EMPTY = ["".join("F" if c == "R" else c for c in r) for r in HEART]
GEM = grid(lambda x, y: ("L" if x < 8 and y < 8 else "G") if abs(x - 7.5) * 1.6 + abs(y - 7.5) < 9 else ".")
KEY = grid(lambda x, y: "Y" if ((x - 5) ** 2 + (y - 5) ** 2 < 12 and (x - 5) ** 2 + (y - 5) ** 2 > 3) or
           (y == 5 and 8 <= x <= 14) or (x in (12, 14) and 6 <= y <= 8) else ".")
CONTAINER = [r.replace("R", "O") for r in HEART]
SHARD = grid(lambda x, y: ("Z" if x < 8 else "Y") if y >= 2 and abs(x - 7.5) <= (y - 1) * 0.55 and y < 14 else ".")
HERMIT = [
    "................", "......EEEE......", ".....EEEEEE.....", ".....SSSSSS.....",
    ".....SKSSKS.....", ".....WWWWWW.....", "....VWWWWWWV....", "...VVVWWWWVVV...",
    "...VVVVWWVVVV...", "..SVVVVVVVVVVS..", "..SVVVVVVVVVVS..", "...VVVVVVVVVV...",
    "...VVVVVVVVVV...", "...VVVVVVVVVV...", "....NNN..NNN....", "................",
]
FIRE = [grid(lambda x, y, k=k: ("Y" if abs(x - 7.5) < (y - 4) * 0.3 and y > 8 else "O") if abs(x - 7.5 + k * (y < 8)) < (y - 2) * 0.45 and 2 < y < 15 else ".") for k in (0, 1)]


def boss(frame):  # Ogloth: a big eye with tentacles, 32x32
    def px(x, y):
        dx, dy = x - 15.5, y - 13
        d = (dx * dx + dy * dy) ** 0.5
        if d < 4 and y < 15:
            return "K"
        if d < 7:
            return "W" if d > 4 else "R"
        if d < 12.5 and y < 22:
            return "V" if d > 11 else "P"
        if y >= 20 and y < 31:
            col = (x + (frame * 2 if (x // 6) % 2 else -frame * 2)) % 6
            if col in (1, 2, 3) and abs(x - 15.5) < 14:
                return "P" if col != 3 else "V"
        return "."
    return grid(px, 32, 32)


def speckle(x, y, every=17):  # scattered, not striped
    return (x * 37 + y * 101 + x * y * 13) % every == 0


GRASS = grid(lambda x, y: "L" if (x * 7 + y * 13) % 23 == 0 or ((x * 7 + y * 13) % 23 == 1 and y % 3 == 0) else "G")
TREE = grid(lambda x, y: ("N" if y > 11 and 5 < x < 10 else ".") if not (x - 7.5) ** 2 + (y - 6.5) ** 2 < 49 else
            ("L" if (x - 5) ** 2 + (y - 4) ** 2 < 6 else ("Q" if speckle(x, y, 5) else "G")))
TREE_ON_GRASS = ["".join(t if t != "." else g for t, g in zip(tr, gr)) for tr, gr in zip(TREE, GRASS)]
ROCK = grid(lambda x, y: ("E" if (x - 6) ** 2 + (y - 6) ** 2 < 12 else "F") if (x - 7.5) ** 2 + (y - 8) ** 2 < 52 else "G")
WATER = [grid(lambda x, y, k=k: "U" if (y + k * 2) % 6 == 0 and (x + y + k) % 8 < 4 else "B") for k in (0, 2)]
SAND = grid(lambda x, y: "M" if (x * 5 + y * 11) % 19 == 0 else "T")
CAVE = grid(lambda x, y: "X" if (x - 7.5) ** 2 / 36 + (y - 15) ** 2 / 110 < 1 else ("F" if (x + y) % 4 else "E"))
WALL = grid(lambda x, y: "C" if y % 8 == 7 or (x + (8 if (y // 8) % 2 else 0)) % 16 == 15 else ("A" if y % 8 else "E"))
FLOOR = grid(lambda x, y: "C" if x % 8 == 0 or y % 8 == 0 else ("F" if speckle(x, y, 11) else "A"))
DOOR = grid(lambda x, y: "Y" if (x - 7.5) ** 2 + (y - 7) ** 2 < 4 or (abs(x - 7.5) < 1 and 7 < y < 11) else
            ("N" if 2 < x < 13 and y > 1 else "C"))
STAIRS = grid(lambda x, y: "K" if y % 4 == 3 else ("E" if y % 4 == 0 else "F"))
STATUE = grid(lambda x, y: ("E" if (x - 7.5) ** 2 + (y - 5) ** 2 < 14 else ("F" if 4 < x < 11 and y > 5 else "A")) if y < 15 else "C")
FLOWERS = grid(lambda x, y: "W" if (x, y) in ((3, 4), (11, 9), (6, 12)) else ("Y" if (x, y) in ((4, 4), (12, 9), (7, 12)) else
                                                                          ("L" if (x * 7 + y * 13) % 23 == 0 else "G")))
FLOOR_DARK = [r.replace("F", "C") for r in FLOOR]


def art():
    for name, rows in (("down", WREN_DOWN), ("up", WREN_UP), ("side", WREN_SIDE)):
        save(f"wren_{name}_0", rows)
        save(f"wren_{name}_1", legs(rows, 1))
    save("sword", SWORD)
    save("slime_0", SLIME)
    save("slime_1", SLIME_SQUASH)
    save("bat_0", BAT_UP)
    save("bat_1", BAT_DOWN)
    save("skull_0", SKULL)
    save("skull_1", SKULL2)
    for i, rows in enumerate(POOF):
        save(f"poof_{i}", rows)
    save("heart", HEART)
    save("heart_half", HEART_HALF)
    save("heart_empty", HEART_EMPTY)
    save("gem", GEM)
    save("key", KEY)
    save("container", CONTAINER)
    save("shard", SHARD)
    save("hermit", HERMIT)
    save("fire_0", FIRE[0])
    save("fire_1", FIRE[1])
    save("boss_0", boss(0))
    save("boss_1", boss(1))
    for name, rows in (("grass", GRASS), ("tree", TREE_ON_GRASS), ("rock", ROCK), ("water_0", WATER[0]), ("water_1", WATER[1]),
                       ("sand", SAND), ("cave", CAVE), ("wall", WALL), ("floor", FLOOR), ("floor_dark", FLOOR_DARK),
                       ("door", DOOR), ("stairs", STAIRS), ("statue", STATUE), ("flowers", FLOWERS)):
        save(name, rows)
    path = os.path.join(ROOT, "assets", "atlases", "sprites.atlas.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump({"sources": sources, "filter": "nearest", "padding": 2}, f, indent=2)


# ---- Audio ----------------------------------------------------------------------

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
    return np.random.default_rng(5).uniform(-1, 1, len(x)) * np.exp(-decay * x)


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
    write("swing.wav", noise(0.12, 25) * np.sin(np.linspace(0, np.pi, int(0.12 * SR))), 0.45)
    write("hit.wav", square(hz("C3"), 0.1, 0.5, 30) + 0.5 * noise(0.1, 30), 0.6)
    write("hurt.wav", sweep(600, 150, 0.25, 0.25) * np.exp(-6 * t(0.25)), 0.6)
    write("defeat.wav", noise(0.35, 9) + 0.4 * sweep(400, 60, 0.35), 0.6)
    write("gem.wav", notes(["E6", "A6"], 0.06, 0.5, 8), 0.4)
    write("heart.wav", notes(["C6", "E6", "G6"], 0.05, 0.5, 8), 0.4)
    write("key.wav", notes(["G5", "C6", "E6", "G6"], 0.06, 0.5, 6), 0.45)
    write("item.wav", notes(["A4", "C#5", "E5", "A5", "-", "A5", "C#6", "E6", "A6"], 0.1, 0.5, 2), 0.5)
    write("door.wav", sweep(200, 80, 0.3) * np.exp(-5 * t(0.3)) + 0.3 * noise(0.3, 10), 0.6)
    write("text.wav", square(hz("A5"), 0.02, 0.5, 60), 0.25)
    write("stairs.wav", notes(["C5", "B4", "A4", "G4", "F4", "E4"], 0.07, 0.5, 6), 0.45)
    write("boss_hit.wav", sweep(900, 120, 0.3) + 0.5 * noise(0.3, 12), 0.7)
    write("die.wav", notes(["E5", "D5", "C5", "B4", "A4", "-", "A3"], 0.15, 0.5, 2), 0.55)
    write("victory.wav", notes(["C5", "E5", "G5", "C6", "-", "G5", "C6", "E6", "G6"], 0.14, 0.5, 1.5), 0.55)
    write("menu.wav", square(hz("A5"), 0.04, 0.5, 30), 0.4)


def song(melody, bass, beat, name, duty=0.5):
    lead = np.concatenate([np.concatenate([square(hz(n), beat * b - 0.015, duty, 2.0), np.zeros(int(0.015 * SR))]) for n, b in melody])
    low = np.concatenate([tri(hz(n), beat * b) for n, b in bass])
    n = min(len(lead), len(low))
    write(name, 0.5 * lead[:n] + 0.5 * low[:n], 0.65, loop=True)


def music():
    # Overworld: a heroic D-major march.
    grove = [("D5", 1.5), ("A4", 0.5), ("D5", 1), ("E5", 0.5), ("F#5", 0.5), ("G5", 1), ("F#5", 1), ("E5", 2),
             ("C#5", 1.5), ("A4", 0.5), ("C#5", 1), ("D5", 0.5), ("E5", 0.5), ("F#5", 1), ("E5", 1), ("D5", 2),
             ("B4", 1), ("D5", 1), ("G5", 1), ("F#5", 1), ("E5", 1), ("C#5", 1), ("A4", 2),
             ("B4", 1), ("C#5", 1), ("D5", 1), ("E5", 1), ("F#5", 2), ("D5", 2)]
    grove_bass = [(n, 1) for n in ["D3", "A2"] * 4 + ["A2", "E3"] * 4 + ["G2", "D3"] * 2 + ["A2", "E3"] * 2 + ["G2", "A2", "D3", "A2"] * 2]
    song(grove, grove_bass, 0.22, "music_grove.wav")
    # Dungeon: a slow, uneasy minor loop.
    crypt = [("A4", 1), ("C5", 1), ("B4", 1), ("G#4", 1), ("A4", 2), ("-", 2),
             ("F4", 1), ("A4", 1), ("G#4", 1), ("E4", 1), ("F4", 2), ("-", 2)] * 2
    crypt_bass = [("A2", 2), ("E2", 2), ("A2", 2), ("E2", 2), ("F2", 2), ("C3", 2), ("E2", 2), ("E2", 2)] * 2
    song(crypt, crypt_bass, 0.26, "music_crypt.wav", 0.25)
    # Boss: fast and driving.
    boss_m = [("E5", 0.5), ("E5", 0.5), ("G5", 0.5), ("E5", 0.5), ("A5", 0.5), ("G5", 0.5), ("F#5", 0.5), ("D#5", 0.5)] * 4
    boss_b = [("E2", 0.5), ("E3", 0.5)] * 16
    song(boss_m, boss_b, 0.2, "music_boss.wav", 0.25)


if __name__ == "__main__":
    art()
    sfx()
    music()
    print("assets written")
