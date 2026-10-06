#!/usr/bin/env python3
"""Generates Tetris's block art, sound effects and music (Korobeiniki, public domain).
Re-run after editing: python3 tools/gen_assets.py  (needs numpy and Pillow)."""
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets")
SR = 22050


# ---- Art ----------------------------------------------------------------------

def block():
    """A bevelled light-gray cell; sprites tint it per piece."""
    n = 16
    img = Image.new("RGBA", (n, n))
    for y in range(n):
        for x in range(n):
            edge = min(x, y, n - 1 - x, n - 1 - y)
            if edge == 0:
                v = 120 if (x == n - 1 or y == n - 1) else 255  # dark bottom/right rim
            elif edge == 1:
                v = 160 if (x >= n - 2 or y >= n - 2) else 245
            else:
                v = 215 - (x + y) * 2  # soft diagonal shading
            img.putpixel((x, y), (v, v, v, 255))
    path = os.path.join(ROOT, "textures", "block.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


# ---- Audio --------------------------------------------------------------------

def t(seconds):
    return np.arange(int(seconds * SR)) / SR


def hz(note):
    if note in (None, "-"):
        return 0.0
    semis = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[note[0]]
    rest = note[1:]
    if rest[0] == "#":
        semis, rest = semis + 1, rest[1:]
    return 440.0 * 2 ** ((12 * (int(rest) + 1) + semis - 69) / 12)


def tone(freq, seconds, duty=0.25, decay=0.0):
    x = t(seconds)
    if freq == 0:
        return np.zeros_like(x)
    wave_ = np.where(((x * freq) % 1.0) < duty, 1.0, -1.0)
    return wave_ * np.exp(-decay * x)


def tri(freq, seconds):
    x = t(seconds)
    return np.zeros_like(x) if freq == 0 else 2 * np.abs(2 * ((x * freq) % 1.0) - 1) - 1


def sweep(f0, f1, seconds, duty=0.5):
    x = t(seconds)
    phase = np.cumsum(np.linspace(f0, f1, len(x)) / SR)
    return np.where((phase % 1.0) < duty, 1.0, -1.0)


def noise(seconds, decay):
    x = t(seconds)
    return np.random.default_rng(7).uniform(-1, 1, len(x)) * np.exp(-decay * x)


def arpeggio(notes, step, duty=0.5):
    return np.concatenate([tone(hz(n), step, duty, 6) for n in notes])


def write(name, samples, gain=0.8, loop=False):
    peak = np.max(np.abs(samples)) or 1.0
    data = np.clip(samples / peak * gain, -1, 1)
    if not loop:  # short fade so one-shots never click
        k = min(len(data), int(0.006 * SR))
        data[len(data) - k:] *= np.linspace(1, 0, k)
    path = os.path.join(ROOT, "sounds", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((data * 32767).astype("<i2").tobytes())


def sfx():
    write("move.wav", tone(hz("C6"), 0.03, 0.5, 40), 0.35)
    write("rotate.wav", sweep(700, 1100, 0.05), 0.35)
    write("hold.wav", arpeggio(["E5", "B5"], 0.04), 0.4)
    write("lock.wav", tone(hz("C3"), 0.08, 0.5, 30) + 0.4 * noise(0.08, 50), 0.6)
    write("hard_drop.wav", sweep(500, 80, 0.09) * np.exp(-20 * t(0.09)) + 0.6 * noise(0.09, 35), 0.75)
    write("clear.wav", arpeggio(["C5", "E5", "G5", "C6"], 0.05), 0.55)
    write("tetris.wav", arpeggio(["C5", "E5", "G5", "C6", "E6", "G6", "C7", "G6", "C7"], 0.05), 0.65)
    write("level_up.wav", arpeggio(["G5", "C6", "E6", "G6", "C7"], 0.07), 0.6)
    write("game_over.wav", arpeggio(["E5", "D5", "C5", "B4", "A4", "E4", "A3"], 0.14, 0.25), 0.6)
    write("menu_move.wav", tone(hz("A5"), 0.035, 0.5, 30), 0.4)
    write("menu_select.wav", arpeggio(["E5", "A5", "E6"], 0.05), 0.5)


# Korobeiniki in eighth notes: (note, eighths).
PHRASE_A = [("E5", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("C5", 1), ("B4", 1), ("A4", 2), ("A4", 1), ("C5", 1),
            ("E5", 2), ("D5", 1), ("C5", 1), ("B4", 3), ("C5", 1), ("D5", 2), ("E5", 2), ("C5", 2), ("A4", 2),
            ("A4", 4)]
PHRASE_B = [("D5", 3), ("F5", 1), ("A5", 2), ("G5", 1), ("F5", 1), ("E5", 3), ("C5", 1), ("E5", 2), ("D5", 1),
            ("C5", 1), ("B4", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("E5", 2), ("C5", 2), ("A4", 2), ("A4", 4)]
BASS_A = ["E2", "A2", "E2", "A2"]  # one root per bar, bounced in octaves
BASS_B = ["D2", "C2", "E2", "A2"]


def music():
    eighth = 0.2  # 150 bpm
    lead = []
    for note, length in PHRASE_A + PHRASE_A + PHRASE_B + PHRASE_B:
        lead.append(tone(hz(note), eighth * length - 0.02, 0.5, 2.5))
        lead.append(np.zeros(int(0.02 * SR)))
    bass = []
    for root in BASS_A + BASS_A + BASS_B + BASS_B:
        low, high = hz(root), hz(root[0] + str(int(root[-1]) + 1))
        for k in range(8):
            bass.append(tri(low if k % 2 == 0 else high, eighth))
    lead, bass = np.concatenate(lead), np.concatenate(bass)
    n = min(len(lead), len(bass))
    write("music.wav", 0.55 * lead[:n] + 0.45 * bass[:n], 0.7, loop=True)


if __name__ == "__main__":
    block()
    sfx()
    music()
    print("assets written to", os.path.normpath(ROOT))
