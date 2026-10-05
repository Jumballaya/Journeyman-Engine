#!/usr/bin/env python3
"""Generates Embers of Aldane's pixel art (one PNG per frame + the atlas
config), sound effects and music. All original. Re-run after editing:
python3 tools/gen_assets.py  (needs numpy and Pillow)."""
import json
import math
import os
import sys
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.dirname(__file__))
SR = 22050

# ---- Art ------------------------------------------------------------------------
# Characters, monsters and tiles come from characters.py, monsters.py and
# terrain.py (shaded with pixelart.py); this file adds backdrops, effects and
# UI pieces and writes everything plus the atlas config.

import characters as people
import monsters
import terrain
from pixelart import Canvas, Material, ramp

sources = []


def store(name, img):
    path = os.path.join(ROOT, "assets", "textures", name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)
    sources.append("assets/textures/" + name + ".png")


def dither(level, x, y):
    """Ordered (Bayer 2x2) dithering of a fractional palette level."""
    base = math.floor(level)
    return base + (1 if level - base > (0.25, 0.75, 1.0, 0.5)[(x % 2) + (y % 2) * 2] else 0)


def gradient_color(stops, t):
    t = max(0.0, min(1.0, t)) * (len(stops) - 1)
    i = min(int(t), len(stops) - 2)
    f = t - i
    return tuple(int(stops[i][k] * (1 - f) + stops[i + 1][k] * f) for k in range(3))


CLOUDS = [(40, 22, 26, 7), (70, 18, 18, 6), (150, 30, 30, 8), (182, 26, 16, 6), (262, 16, 24, 6), (290, 22, 14, 5)]


def cloud_puff(x, y):
    """How deep inside a puffy cloud (x, y) is: > 0 inside, lit toward the top."""
    best = 0.0
    for cx, cy, rx, ry in CLOUDS:
        d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
        for bx in (-0.5, 0.0, 0.5):  # three bumps along the top
            d = min(d, ((x - cx - bx * rx) / (rx * 0.45)) ** 2 + ((y - cy + ry * 0.6) / (ry * 0.75)) ** 2)
        if d < 1:
            best = max(best, (1 - d) * (1.2 - max(0, y - cy) / ry))
    return best


def backdrop_forest():
    w, h = 320, 168
    img = Image.new("RGBA", (w, h))
    P = img.load()
    sky = ramp("#2a4a9a", "#3a64b8", "#5a86d0", "#86aee4", "#c4dcf4")
    far = ramp("#3a5a8a", "#4a6c9a", "#5c80aa")
    mid = ramp("#0e2e1e", "#163e24", "#205230", "#2e6a3a", "#3e8446")
    ground = terrain.GRASS
    for y in range(h):
        for x in range(w):
            if y < 78:
                c = sky[min(4, dither(y / 78 * 4.2, x, y))]
                puff = cloud_puff(x, y)
                if puff > 0:
                    c = (240, 246, 255) if puff > 0.55 else (200, 214, 240) if puff > 0.2 else (164, 186, 226)
                mount = 58 - 16 * abs(math.sin(x * 0.018 + 0.4)) - 6 * abs(math.sin(x * 0.051))
                if y > mount:
                    c = far[min(2, dither((y - mount) / 14, x, y))]
            else:
                c = None
            tree_top = 74 - 9 * abs(math.sin(x * 0.11)) - 5 * abs(math.sin(x * 0.27 + 1)) - 3 * terrain.h2(x // 3, 0, 7)
            if y >= tree_top and y < 104:
                depth = (y - tree_top) / 30
                lit = 1.0 if terrain.h2(x // 2, y // 2, 8) > 0.7 else 0.0
                c = mid[max(0, min(4, dither(3.4 - depth * 3 + lit, x, y)))]
            if y >= 104:
                t = (y - 104) / 64
                n = terrain.smooth(x % 16, y % 16, 4, 3) * 0.8 + terrain.h2(x, y, 9) * 0.5
                c = ground[max(0, min(4, int(1.4 + t * 1.6 + n)))]
                if y < 108:
                    c = mid[1]  # the tree line's shadow on the grass
            P[x, y] = c + (255,)
    return img


def backdrop_lair():
    w, h = 320, 168
    img = Image.new("RGBA", (w, h))
    P = img.load()
    rock = ramp("#140808", "#26100e", "#3a1a14", "#56281c", "#7a3a24")
    lava = ramp("#7a1404", "#c03008", "#f06a10", "#ffb030", "#fff0a0")
    for y in range(h):
        for x in range(w):
            ceiling = 26 + 12 * abs(math.sin(x * 0.07)) + 8 * terrain.h2(x // 4, 1, 3)
            stalactite = (x % 37) < 6 and y < ceiling + 14 - abs((x % 37) - 3) * 4
            floor_y = 74 + 5 * math.sin(x * 0.03)   # a lava river far behind the fighters
            if y < ceiling or stalactite:
                c = rock[min(4, int(1 + terrain.h2(x // 2, y // 2, 4) * 1.6))]
            elif y < floor_y:
                glow = (y - ceiling) / (floor_y - ceiling)
                c = gradient_color([(16, 6, 8), (52, 14, 12), (130, 40, 16)], glow)
                if terrain.h2(x, y, 6) > 0.997:
                    c = (255, 180, 60)                      # drifting embers
            elif y < floor_y + 10:
                c = lava[min(4, dither(1.5 + math.sin(x * 0.2 + y * 0.6) * 1.2 + (y - floor_y) / 10, x, y))]
            else:
                c = rock[min(4, int(1.5 + terrain.smooth(x % 16, y % 16, 4, 2) * 2 + (0.8 if (x + y) % 9 == 0 else 0)))]
            P[x, y] = c + (255,)
    return img


def effect(kind, frame):
    c = Canvas(32, 32)
    r = 5 + frame * 4
    if kind == "slash":
        blade = Material(ramp("#6a8ad0", "#a8c4ff", "#e8f0ff", "#ffffff"), outline=False)
        for i in range(10):
            t = (i / 9 - 0.5) * 1.6
            c.ellipse(16 + math.sin(t) * r * 1.3, 16 - math.cos(t) * r * 1.3, 1.6 - abs(t) * 0.6, 1.6 - abs(t) * 0.6, blade)
    elif kind == "fire":
        outer = Material(ramp("#8a1a04", "#d8400a", "#ff8020", "#ffc040"), outline=False)
        inner = Material(ramp("#ffc040", "#ffe880", "#ffffff"), outline=False)
        for i in range(7):
            a = i / 7 * math.pi * 2 + frame
            c.ellipse(16 + math.cos(a) * r * 0.6, 18 + math.sin(a) * r * 0.4 - frame * 2, r * 0.45, r * 0.6, outer)
        c.ellipse(16, 18 - frame * 2, r * 0.5, r * 0.6, inner)
    elif kind == "ice":
        ice = Material(ramp("#2a6ab0", "#6ab0ff", "#c8eaff", "#ffffff"))
        for i in range(5):
            a = i / 5 * math.pi * 2 + 0.3
            x, y = 16 + math.cos(a) * r * 0.5, 16 + math.sin(a) * r * 0.5
            c.poly([(x, y - r * 0.6), (x + 2.5, y), (x, y + r * 0.6), (x - 2.5, y)], ice)
    elif kind == "bolt":
        bolt = Material(ramp("#d0a000", "#ffe040", "#fffbc0", "#ffffff"), outline=False)
        x = 16
        for y in range(0, min(32, 8 + frame * 12), 4):
            nx = 16 + (6 if (y // 4) % 2 else -6) * (1 - y / 40)
            c.line(x, y, nx, y + 4, bolt, 2.2)
            x = nx
    elif kind == "heal":
        spark = Material(ramp("#3aa860", "#80f0a0", "#e0ffe8", "#ffffff"), outline=False)
        for i in range(8):
            x = 6 + terrain.h2(i, 1, 5) * 20
            y = 28 - ((terrain.h2(i, 2, 5) * 20 + frame * 8) % 26)
            c.line(x - 2, y, x + 2, y, spark)
            c.line(x, y - 2, x, y + 2, spark)
    return c.render(outline=False)


def window_texture():
    img = Image.new("RGBA", (8, 64))
    P = img.load()
    for y in range(64):
        for x in range(8):
            P[x, y] = gradient_color([(58, 86, 200), (30, 46, 138), (14, 22, 84)], y / 63) + (255,)
    return img


def hand_cursor():
    glove = Material(ramp("#6a6a7a", "#b0b4c4", "#e8eaf4", "#ffffff"))
    c = Canvas(16, 12)
    c.rect(2, 4, 9, 9, glove)          # palm
    c.rect(8, 4, 15, 6, glove)         # pointing finger
    c.rect(5, 2, 8, 4, glove, -0.4)    # thumb
    return c.render()


def art():
    for name, hero in (("kael", people.KAEL), ("lyra", people.LYRA), ("bram", people.BRAM)):
        for pose in ("idle", "attack", "cast", "hurt"):
            store(f"{name}_{pose}", people.battler(hero, pose).render())
        store(f"{name}_ko", people.knocked_out(hero).render())
    for name, person in (("walk", people.KAEL), ("elder", people.ELDER), ("inn", people.INNKEEPER),
                         ("smith", people.SMITH), ("child", people.CHILD)):
        for facing in ("down", "up", "side"):
            for frame in (0, 1):
                if name == "walk" or frame == 0:
                    store(f"{name}_{facing}_{frame}", people.walker(person, facing, frame).render())
    for frame in (0, 1):
        store(f"jelly_{frame}", monsters.jelly(frame).render())
        store(f"goblin_{frame}", monsters.goblin(frame).render())
        store(f"wisp_{frame}", monsters.wisp(frame).render())
        store(f"wyrm_{frame}", monsters.wyrm(frame).render())
    for name, img in terrain.all_tiles().items():
        store(name, img)
    for kind in ("slash", "fire", "ice", "bolt", "heal"):
        for frame in range(3):
            store(f"{kind}_{frame}", effect(kind, frame))
    store("bg_forest", backdrop_forest())
    store("bg_lair", backdrop_lair())
    store("ui_window", window_texture())
    store("hand", hand_cursor())
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
