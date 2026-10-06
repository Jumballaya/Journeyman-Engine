#!/usr/bin/env python3
"""Generates Super Pip's pixel art (one PNG per frame + the atlas config), sound
effects and music. All original. Re-run after editing: python3 tools/gen_assets.py
(needs numpy and Pillow)."""
import json
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
TEX = os.path.join(ROOT, "assets", "textures")
SR = 22050

# ---- Art ------------------------------------------------------------------------
# Sprites are ASCII grids; each letter maps to a color, "." is transparent.

PALETTE = {
    "K": (24, 20, 28), "W": (248, 248, 240), "S": (250, 196, 150), "C": (52, 98, 214),
    "R": (226, 72, 52), "G": (54, 150, 66), "N": (122, 74, 40), "Y": (252, 214, 72),
    "O": (238, 132, 40), "D": (30, 92, 44), "L": (140, 220, 110), "B": (180, 100, 56),
    "M": (108, 56, 30), "T": (236, 180, 92), "P": (64, 186, 76), "Q": (24, 110, 40),
    "E": (150, 150, 162), "F": (96, 96, 110), "A": (232, 80, 24), "H": (255, 190, 60),
    "U": (120, 180, 255), "V": (210, 230, 255), "X": (60, 60, 72), "Z": (255, 240, 180),
}
SWAPS = {  # palette swaps for themed tiles
    "underground": {"B": (60, 120, 200), "M": (24, 48, 96), "T": (130, 180, 240), "G": (60, 120, 200), "L": (130, 180, 240), "D": (24, 48, 96)},
    "castle": {"B": (150, 150, 162), "M": (80, 80, 92), "T": (200, 200, 210), "G": (150, 150, 162), "L": (200, 200, 210), "D": (80, 80, 92)},
}

sources = []


def save(name, rows, swap=None):
    h, w = len(rows), max(len(r) for r in rows)
    img = Image.new("RGBA", (w, h))
    colors = dict(PALETTE, **(swap or {}))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), colors[ch] + (255,))
    path = os.path.join(TEX, name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)
    sources.append("assets/textures/" + name + ".png")


def flipx(rows):
    return [r[::-1] for r in rows]


# Pip, 16x16 when small. Head and torso shared; legs vary per frame.
PIP_HEAD = [
    "................",
    ".....CCCCC......",
    "....CCCWCCCCC...",
    "....CCCCCCCCCC..",
    "....SSSSSS......",
    "...SSSKSSKS.....",
    "...SSSSSSSS.....",
    "....SSSSSS......",
]
PIP_TORSO = [
    "....RRRRRR......",
    "...RRRRRRRR.....",
    "..SRRGRRGRRS....",
    "..SSGGGGGGSS....",
    "....GGGGGG......",
]
PIP_LEGS = {
    "idle": ["....GG..GG......", "...NNN..NNN.....", "...NNN..NNN....."],
    "walk1": ["...GG....GG.....", "..NNN.....NN....", "..NN......NNN..."],
    "walk2": [".....GGGG.......", "....NNNN........", "....NNNNN......."],
    "jump": ["...GG.....GG....", "..NNN......NN...", "..N.........N..."],
}
PIP_JUMP_TORSO = [
    "..S.RRRRRR.S....",
    "..SRRRRRRRRS....",
    "...RRGRRGRR.....",
    "....GGGGGG......",
    "....GGGGGG......",
]


def pip(size):
    for frame, legs in PIP_LEGS.items():
        torso = PIP_JUMP_TORSO if frame == "jump" else PIP_TORSO
        if size == "small":
            rows = PIP_HEAD + torso + legs
        else:  # big: 16x24, a taller torso and legs
            rows = PIP_HEAD + [r for r in torso for _ in (0, 1)][:10] + [l for l in legs for _ in (0, 1)]
        save(f"pip_{size}_{frame}", rows)
    dead = PIP_HEAD[:5] + ["...SSSKSSKS.....".replace("K", "X")] + PIP_HEAD[6:] + PIP_TORSO + PIP_LEGS["idle"]
    if size == "small":
        save("pip_small_dead", dead)


GLOOP = [
    "................", "................", "................", "................",
    "......GGGG......",
    "....GGGGGGGG....",
    "...GGLGGGGGGG...",
    "..GGLLGGGGGGGG..",
    "..GGGWKGGWKGGG..",
    ".GGGGWKGGWKGGGG.",
    ".GGGGGGGGGGGGGG.",
    ".GGGGGGGGGGGGGG.",
    ".GGGDGGGGGGDGGG.",
    "..GGGGGGGGGGGG..",
    "..DDDDDDDDDDDD..",
    "................",
]
GLOOP_SQUASH = ["................"] * 6 + [
    "....GGGGGGGG....",
    "..GGLLGGGGGGGG..",
    ".GGGGWKGGWKGGGG.",
    "GGGGGWKGGWKGGGGG",
    "GGGGGGGGGGGGGGGG",
    "GGGDGGGGGGGGDGGG",
    ".GGGGGGGGGGGGGG.",
    ".DDDDDDDDDDDDDD.",
    "................", "................",
]
GLOOP_FLAT = ["................"] * 11 + [
    "...GGGGGGGGGG...",
    ".GGGWKGGGGWKGGG.",
    "GGGGGGGGGGGGGGGG",
    ".DDDDDDDDDDDDDD.",
    "................",
]
BEETLE = ["................"] * 5 + [
    ".......OOOOO....",
    ".....OOHHOOOOO..",
    "....OOHOOOOOOOO.",
    "...OOOOOOOOOOOO.",
    ".KKOOOOOOOOOOOO.",
    "KWKKMMMMMMMMMMM.",
    "KKKKKKKKKKKKKKK.",
    ".KK..KK...KK....",
    ".K....K....K....",
    "................", "................",
]
BEETLE2 = BEETLE[:12] + [".KK...KK...KK...", "..K....K....K...", "................", "................"]
SHELL = ["................"] * 8 + [
    "....OOOOOOOO....",
    "..OOHHOOOOOOOO..",
    ".OOHOOOOOOOOOOO.",
    ".OOOOOOOOOOOOOO.",
    ".MMMMMMMMMMMMMM.",
    "..KKKKKKKKKKKK..",
    "................", "................",
]
MUSHROOM = [
    "................",
    ".....RRRRRR.....",
    "...RRWWRRRRRR...",
    "..RRWWWRRRWWRR..",
    ".RRRWWRRRRWWWRR.",
    ".RRRRRRRRRRWWRR.",
    "RRWWRRRRRRRRRRRR",
    "RWWWWRRRRRRWWRRR",
    "RRWWRRRRRRWWWWRR",
    ".RRRRRRRRRRRRRR.",
    "...TTTTTTTTTT...",
    "...TTKTTTTKTT...",
    "...TTKTTTTKTT...",
    "...TTTTTTTTTT...",
    "....TTTTTTTT....",
    "................",
]


def coin(width):
    rows = []
    for y in range(16):
        dy = abs(y - 7.5)
        if dy > 6.5:
            rows.append("." * 16)
            continue
        half = max(1, int(width / 2 * (1 - (dy / 7.5) ** 2) ** 0.5 + 0.5))
        line = ["."] * 16
        for x in range(8 - half, 8 + half):
            line[x] = "Y" if x != 8 - half else "H"
        if width > 6 and 3 < y < 12:
            line[8] = "H"
        rows.append("".join(line))
    return rows


def grid(fn):
    return ["".join(fn(x, y) for x in range(16)) for y in range(16)]


def speckle(x, y):  # scattered dark pebbles
    return (x * 37 + y * 101 + x * y * 13) % 17 == 0


GROUND_TOP = grid(lambda x, y: ("L" if y < 2 and (x * 3 + y) % 5 else "G") if y < 4 else ("M" if speckle(x, y) else "B"))
GROUND = grid(lambda x, y: "M" if speckle(x, y) else "B")
BRICK = grid(lambda x, y: "M" if y % 8 == 7 or (x + (8 if y // 8 else 0)) % 16 == 15 else ("T" if y % 8 == 0 else "B"))
HARD = grid(lambda x, y: "M" if x == 15 or y == 15 else ("T" if x == 0 or y == 0 else ("B" if (x + y) % 2 else "T") if 3 < x < 12 and 3 < y < 12 else "B"))
QMARK = [".YYY.", "Y...Y", "....Y", "...Y.", "..Y..", ".....", "..Y.."]  # 5x7 "?"


def qblock(shade):
    """A ? block; shade 0..2 pulses the face color."""
    face = "YHO"[shade]

    def px(x, y):
        if x == 15 or y == 15:
            return "M"
        if x == 0 or y == 0:
            return "Z"
        if (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
            return "N"
        if 5 <= x < 10 and 4 <= y < 11 and QMARK[y - 4][x - 5] == "Y":
            return "M"
        return face
    return grid(px)


USED = grid(lambda x, y: "M" if x == 15 or y == 15 or x == 0 or y == 0 else ("N" if (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)) else "B"))
PIPE_TOP_L = grid(lambda x, y: "Q" if y in (0, 15) or x == 0 else ("L" if x in (2, 3) else "P"))
PIPE_TOP_R = grid(lambda x, y: "Q" if y in (0, 15) or x == 15 else ("D" if x in (11, 12, 13) else "P"))
PIPE_L = grid(lambda x, y: "Q" if x == 2 else ("." if x < 2 else ("L" if x in (4, 5) else "P")))
PIPE_R = grid(lambda x, y: "Q" if x == 13 else ("." if x > 13 else ("D" if x in (9, 10, 11) else "P")))
POLE = grid(lambda x, y: "L" if x == 7 else ("D" if x == 8 else "."))
POLE_TOP = grid(lambda x, y: ("L" if (x - 7.5) ** 2 + (y - 8) ** 2 < 12 else ".") if y > 2 else ".")
FLAG = grid(lambda x, y: ("W" if x + 2 * abs(y - 7) < 14 else ".") if 1 < y < 14 and x < 14 else ".")
LAVA1 = grid(lambda x, y: "Y" if y < 3 and (x + y) % 4 == 0 else ("A" if y < 6 else "R"))
LAVA2 = grid(lambda x, y: "Y" if y < 3 and (x + y + 2) % 4 == 0 else ("A" if y < 6 else "R"))
BRIDGE = grid(lambda x, y: "N" if y < 4 else ("M" if y < 6 else ("N" if x in (2, 13) and y < 12 else ".")))
GEM = grid(lambda x, y: ("V" if abs(x - 7.5) + abs(y - 8) < 4 else "U") if abs(x - 7.5) + abs(y - 8) < 7 else ".")
DEBRIS = ["BBMM.", "BTBM.", "MBBB.", ".MBM."]
CLOUD = [
    "..........WWWW..................",
    "........WWWWWWWW......WWWW......",
    "......WWWWWWWWWWWW..WWWWWWWW....",
    "....WWWWWWWWWWWWWWWWWWWWWWWWWW..",
    "..WWWWWWWWWWWWWWWWWWWWWWWWWWWWW.",
    ".WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    ".WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "..VVVVVVVVVVVVVVVVVVVVVVVVVVVVV.",
]
BUSH = [r.replace("W", "P").replace("V", "Q") for r in CLOUD]
HILL = ["".join("." if abs(x - 23.5) > y * 1.5 + 2 else ("D" if (x + y) % 9 == 0 and y > 4 else "G") for x in range(48)) for y in range(16)]


def castle():
    rows = []
    for y in range(48):
        line = ""
        for x in range(48):
            top_wall = y >= 16
            crenel = 8 <= y < 16 and 8 <= x < 40 and (x // 4) % 2 == 0
            tower = y < 16 and 16 <= x < 32 and (y >= 4 and (y >= 8 or (x // 4) % 2 == 0))
            door = y >= 32 and 18 <= x < 30 and (y >= 36 or 20 <= x < 28)
            window = 20 <= y < 26 and (12 <= x < 16 or 32 <= x < 36) or 4 <= y < 7 and 22 <= x < 26 and y >= 8
            if door:
                line += "K"
            elif window:
                line += "K"
            elif top_wall or crenel or tower:
                line += "M" if (y % 8 == 7 or (x + (4 if (y // 8) % 2 else 0)) % 8 == 7) else "B"
            else:
                line += "."
        rows.append(line)
    return rows


def king():  # the Gloop King, 32x32: a big gloop with a crown
    big = [r for r in GLOOP for _ in (0, 1)]
    big = ["".join(c * 2 for c in r) for r in big]
    crown = ["." * 10 + "YY..YY..YY" + "." * 12, "." * 10 + "YYYYYYYYYY" + "." * 12, "." * 10 + "YRYYRYYRYY" + "." * 12]
    return crown + big[3 + 3:] + ["." * 32] * 3


def art():
    pip("small")
    pip("big")
    save("gloop_1", GLOOP)
    save("gloop_2", GLOOP_SQUASH)
    save("gloop_flat", GLOOP_FLAT)
    save("beetle_1", BEETLE)
    save("beetle_2", BEETLE2)
    save("shell", SHELL)
    save("mushroom", MUSHROOM)
    for i, w in enumerate((10, 6, 2, 6)):
        save(f"coin_{i}", coin(w))
    for theme, swap in (("over", None), ("under", SWAPS["underground"]), ("castle", SWAPS["castle"])):
        save(f"{theme}_ground_top", GROUND_TOP, swap)
        save(f"{theme}_ground", GROUND, swap)
        save(f"{theme}_brick", BRICK, swap)
        save(f"{theme}_hard", HARD, swap)
    for i in range(3):
        save(f"qblock_{i}", qblock(i))
    save("used", USED)
    save("pipe_top_l", PIPE_TOP_L)
    save("pipe_top_r", PIPE_TOP_R)
    save("pipe_l", PIPE_L)
    save("pipe_r", PIPE_R)
    save("pole", POLE)
    save("pole_top", POLE_TOP)
    save("flag", FLAG)
    save("lava_1", LAVA1)
    save("lava_2", LAVA2)
    save("bridge", BRIDGE)
    save("gem", GEM)
    save("debris", DEBRIS)
    save("cloud", CLOUD)
    save("bush", BUSH)
    save("hill", HILL)
    save("castle", castle())
    save("king_1", king())
    save("king_2", [r for r in king()][2:] + ["." * 32] * 2)
    atlas = {"sources": sources, "filter": "nearest", "padding": 2}
    path = os.path.join(ROOT, "assets", "atlases", "sprites.atlas.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(atlas, f, indent=2)


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
    return np.random.default_rng(3).uniform(-1, 1, len(x)) * np.exp(-decay * x)


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
    write("jump.wav", sweep(300, 900, 0.16, 0.25) * np.exp(-6 * t(0.16)), 0.45)
    write("big_jump.wav", sweep(200, 600, 0.2, 0.25) * np.exp(-5 * t(0.2)), 0.45)
    write("coin.wav", np.concatenate([square(hz("B5"), 0.06, 0.5), square(hz("E6"), 0.3, 0.5, 9)]), 0.45)
    write("stomp.wav", sweep(500, 120, 0.1) * np.exp(-18 * t(0.1)), 0.6)
    write("bump.wav", square(hz("C3"), 0.08, 0.5, 25) + 0.3 * noise(0.08, 40), 0.6)
    write("break.wav", noise(0.25, 14) + 0.4 * square(hz("C2"), 0.25, 0.5, 14), 0.7)
    write("sprout.wav", notes(["C4", "G4", "C5", "D4", "A4", "D5", "E4", "B4", "E5"], 0.04, 0.5, 2), 0.45)
    write("powerup.wav", notes(["C5", "G4", "C5", "E5", "G5", "C6", "G5", "E6"], 0.05, 0.5, 3), 0.5)
    write("shrink.wav", notes(["G5", "D5", "G4", "D4", "G3"], 0.05, 0.25, 3), 0.5)
    write("kick.wav", square(hz("A5"), 0.05, 0.5, 30), 0.45)
    write("oneup.wav", notes(["E5", "G5", "E6", "C6", "D6", "G6"], 0.08, 0.5, 2), 0.5)
    write("die.wav", np.concatenate([notes(["B4", "F5", "-", "F5", "F5", "E5", "D5", "C5"], 0.12, 0.5, 2), np.zeros(4000)]), 0.55)
    write("flagpole.wav", sweep(1200, 200, 0.9, 0.25), 0.4)
    write("clear.wav", notes(["G4", "C5", "E5", "G5", "C6", "E6", "G6", "-", "E6", "-", "G6"], 0.11, 0.5, 1.5), 0.55)
    write("game_over.wav", notes(["C5", "-", "G4", "-", "E4", "A4", "B4", "A4", "G#4", "A#4", "G#4", "G4", "F4", "G4"], 0.13, 0.5, 2), 0.55)
    write("boss_hit.wav", sweep(800, 100, 0.3) + 0.5 * noise(0.3, 10), 0.7)
    write("menu.wav", square(hz("A5"), 0.04, 0.5, 30), 0.4)


def song(melody, bass, beat, name, lead_duty=0.5):
    """melody/bass: [(note, beats)]; both loop over the same length."""
    def render(seq, voice):
        return np.concatenate([voice(hz(n), beat * b) for n, b in seq])
    lead = render(melody, lambda f, s: np.concatenate([square(f, s - 0.015, lead_duty, 2.5), np.zeros(int(0.015 * SR))]))
    low = render(bass, lambda f, s: tri(f, s))
    n = min(len(lead), len(low))
    write(name, 0.5 * lead[:n] + 0.5 * low[:n], 0.65, loop=True)


def music():
    # Overworld: bright C major, two 8-bar phrases.
    over_a = [("E5", 1), ("G5", 1), ("C6", 1), ("G5", 1), ("A5", 1.5), ("G5", 0.5), ("E5", 1),
              ("F5", 1), ("A5", 1), ("G5", 1), ("E5", 1), ("D5", 1), ("E5", 1), ("C5", 2),
              ("E5", 1), ("G5", 1), ("C6", 1), ("D6", 1), ("E6", 1.5), ("D6", 0.5), ("C6", 1), ("A5", 1),
              ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1), ("C5", 4)]
    over_bass = [(n, 1) for bar in ("C3", "C3", "F3", "C3", "C3", "A2", "G2", "C3") for n in (bar, bar[0] + str(int(bar[-1]) + 1), bar, "G2" if bar == "C3" else bar)]
    song(over_a, over_bass, 0.16, "music_over.wav", 0.25)
    # Underground: sparse minor riff.
    under = [("A4", 0.5), ("-", 0.5), ("A4", 0.5), ("C5", 0.5), ("-", 1), ("E5", 0.5), ("D5", 0.5),
             ("C5", 1), ("A4", 1), ("-", 2)] * 2 + [("F4", 0.5), ("-", 0.5), ("F4", 0.5), ("A4", 0.5), ("-", 1), ("C5", 0.5), ("B4", 0.5),
             ("G#4", 1), ("E4", 1), ("-", 2)] * 2
    under_bass = [("A2", 0.5), ("-", 0.5)] * 16 + [("F2", 0.5), ("-", 0.5)] * 8 + [("E2", 0.5), ("-", 0.5)] * 8
    song(under, under_bass, 0.22, "music_under.wav", 0.5)
    # Castle: ominous chromatic crawl.
    castle_m = [("D5", 1), ("C#5", 1), ("D5", 1), ("A4", 1), ("Bb4", 1), ("A4", 1), ("G#4", 1), ("A4", 1)] * 2 + \
               [("F5", 1), ("E5", 1), ("F5", 1), ("C5", 1), ("C#5", 1), ("D5", 1), ("A4", 2)] * 2
    castle_b = [("D2", 1), ("A2", 1)] * 8 + [("Bb1", 1), ("F2", 1)] * 4 + [("A1", 1), ("E2", 1)] * 4
    song(castle_m, castle_b, 0.2, "music_castle.wav", 0.25)


if __name__ == "__main__":
    art()
    sfx()
    music()
    print("assets written")
