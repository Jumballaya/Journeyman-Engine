#!/usr/bin/env python3
"""Synthesizes Strike Wing's chiptune music and sound effects.

Everything is generated from code (no samples), so the output is ours to
license with the game. Re-run after editing:  python3 tools/gen_audio.py
Requires numpy. Writes 22.05 kHz mono 16-bit WAVs into assets/sounds/.
"""
import os
import wave

import numpy as np

SR = 22050
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds")
rng = np.random.default_rng(1942)


def t(seconds):
    return np.arange(int(seconds * SR)) / SR


def note_hz(name):
    """'A4', 'C#5', 'Eb3' -> Hz. '-' or None -> 0 (rest)."""
    if name in (None, "-", ""):
        return 0.0
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    semis = names[name[0]]
    rest = name[1:]
    if rest[0] == "#":
        semis += 1
        rest = rest[1:]
    elif rest[0] == "b":
        semis -= 1
        rest = rest[1:]
    midi = 12 * (int(rest) + 1) + semis
    return 440.0 * 2 ** ((midi - 69) / 12)


def env(n, attack=0.005, release=0.05, sustain=1.0):
    e = np.ones(n) * sustain
    a = min(n, int(attack * SR))
    r = min(n - a, int(release * SR))
    if a:
        e[:a] = np.linspace(0, sustain, a)
    if r:
        e[n - r:] = np.linspace(sustain, 0, r)
    return e


def pulse(freq, seconds, duty=0.25):
    x = t(seconds)
    if np.ndim(freq) == 0 and freq == 0:
        return np.zeros_like(x)
    phase = np.cumsum(np.broadcast_to(freq, x.shape) / SR)
    return np.where((phase % 1.0) < duty, 1.0, -1.0)


def triangle(freq, seconds):
    x = t(seconds)
    if np.ndim(freq) == 0 and freq == 0:
        return np.zeros_like(x)
    phase = np.cumsum(np.broadcast_to(freq, x.shape) / SR)
    return 2 * np.abs(2 * (phase % 1.0) - 1) - 1


def noise(seconds):
    return rng.uniform(-1, 1, int(seconds * SR))


def lowpass(x, alpha):
    """One-pole lowpass; alpha in (0, 1], smaller = darker."""
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += alpha * (v - acc)
        y[i] = acc
    return y


def sweep(f0, f1, seconds, curve=1.0):
    x = np.linspace(0, 1, int(seconds * SR)) ** curve
    return f0 + (f1 - f0) * x


def write(name, samples, gain=0.9, loop=False):
    peak = np.max(np.abs(samples)) or 1.0
    data = np.clip(samples / peak * gain, -1, 1)
    if not loop:
        # 6 ms fade so one-shots never end on a click.
        k = min(len(data), int(0.006 * SR))
        data[len(data) - k:] *= np.linspace(1, 0, k)
    pcm = (data * 32767).astype("<i2")
    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"wrote {path} ({len(samples) / SR:.2f}s)")


# ---------------------------------------------------------------------------
# Sound effects
# ---------------------------------------------------------------------------

def sfx():
    n = int(0.07 * SR)
    shot = pulse(sweep(1400, 500, 0.07), 0.07, 0.5) * env(n, 0.001, 0.04)
    write("shoot.wav", shot, 0.5)

    n = int(0.12 * SR)
    eshot = triangle(sweep(700, 250, 0.12), 0.12) * env(n, 0.001, 0.08)
    write("enemy_shoot.wav", eshot, 0.55)

    n = int(0.05 * SR)
    hit = (noise(0.05) * 0.6 + pulse(2200, 0.05, 0.5) * 0.4) * env(n, 0.001, 0.04)
    write("hit.wav", hit, 0.5)

    n = int(0.45 * SR)
    boom = lowpass(noise(0.45), 0.25) * np.exp(-t(0.45) * 9)
    boom += np.sin(2 * np.pi * np.cumsum(sweep(120, 40, 0.45)) / SR) * np.exp(-t(0.45) * 10) * 0.8
    write("explode_small.wav", boom, 0.85)

    n = int(1.3 * SR)
    big = lowpass(noise(1.3), 0.12) * np.exp(-t(1.3) * 3.2)
    big += np.sin(2 * np.pi * np.cumsum(sweep(90, 25, 1.3)) / SR) * np.exp(-t(1.3) * 3) * 0.9
    big += lowpass(noise(1.3), 0.5) * np.exp(-t(1.3) * 14) * 0.6
    write("explode_big.wav", big, 0.95)

    n = int(1.1 * SR)
    die = pulse(sweep(900, 60, 1.1, 0.6), 1.1, 0.5) * env(n, 0.001, 0.3) * 0.5
    die += lowpass(noise(1.1), 0.2) * np.exp(-t(1.1) * 4)
    write("player_die.wav", die, 0.9)

    def arpeggio(notes, step, duty=0.25, tail=0.08):
        parts = []
        for nm in notes:
            k = int(step * SR)
            parts.append(pulse(note_hz(nm), step, duty) * env(k, 0.002, step * 0.5))
        out = np.concatenate(parts)
        return np.concatenate([out, np.zeros(int(tail * SR))])

    write("powerup.wav", arpeggio(["C5", "E5", "G5", "C6", "E6", "G6"], 0.045), 0.6)
    write("extra_life.wav", arpeggio(["G5", "C6", "E6", "G6", "E6", "G6", "C7"], 0.07, 0.5), 0.6)
    write("menu_move.wav", arpeggio(["A5"], 0.035, 0.5, 0.01), 0.45)
    write("menu_select.wav", arpeggio(["E5", "A5", "E6"], 0.05, 0.5, 0.02), 0.55)
    write("menu_back.wav", arpeggio(["E5", "A4"], 0.05, 0.5, 0.02), 0.5)

    n = int(1.4 * SR)
    bomb = lowpass(noise(1.4), 0.08) * np.exp(-t(1.4) * 2.2)
    bomb += np.sin(2 * np.pi * np.cumsum(sweep(60, 20, 1.4)) / SR) * np.exp(-t(1.4) * 1.8)
    bomb += pulse(sweep(200, 2400, 1.4, 0.4), 1.4, 0.5) * np.exp(-t(1.4) * 3) * 0.15
    write("bomb.wav", bomb, 0.95)

    seg = 0.3
    siren = np.concatenate([
        pulse(note_hz("A5") if i % 2 == 0 else note_hz("D5"), seg, 0.5) * env(int(seg * SR), 0.01, 0.05)
        for i in range(8)])
    write("warning.wav", siren, 0.55)

    pickup = arpeggio(["E6", "B6"], 0.04, 0.5, 0.02)
    write("pickup.wav", pickup, 0.5)


# ---------------------------------------------------------------------------
# Music: a tiny tracker. Patterns are 16th-note steps; '-' rests, '.' holds.
# ---------------------------------------------------------------------------

def render_track(bpm, voices, bars, loop=True):
    """voices: list of (kind, steps, gain, duty). steps: list of note names,
    '-' rest, '.' continue previous note. 16 steps per bar."""
    step_s = 60.0 / bpm / 4
    total = int(round(bars * 16 * step_s * SR))
    mix = np.zeros(total)
    for kind, steps, gain, duty in voices:
        steps = (steps * (bars * 16 // len(steps) + 1))[: bars * 16]
        i = 0
        while i < len(steps):
            s = steps[i]
            length = 1
            while i + length < len(steps) and steps[i + length] == ".":
                length += 1
            start = int(round(i * step_s * SR))
            dur = length * step_s
            n = int(dur * SR)
            if s not in ("-", "."):
                if kind == "kick":
                    x = np.sin(2 * np.pi * np.cumsum(sweep(150, 45, dur, 0.5)) / SR) * np.exp(-t(dur) * 18)
                elif kind == "snare":
                    x = (lowpass(noise(dur), 0.6) * 0.8 + triangle(190, dur) * 0.3) * np.exp(-t(dur) * 22)
                elif kind == "hat":
                    x = noise(dur) * np.exp(-t(dur) * 70)
                elif kind == "tri":
                    x = triangle(note_hz(s), dur) * env(n, 0.003, 0.02)
                else:  # pulse lead
                    f = note_hz(s)
                    vib = f * (1 + 0.004 * np.sin(2 * np.pi * 5.5 * t(dur)) * (t(dur) > 0.12))
                    x = pulse(vib, dur, duty) * env(n, 0.004, min(0.06, dur * 0.4), 0.85)
                end = min(total, start + len(x))
                mix[start:end] += x[: end - start] * gain
            i += length
    if loop:
        # Echo for space; the tail wraps around so the loop point is seamless.
        d = int(step_s * 3 * SR)
        wet = np.roll(mix, d) * 0.22
        mix = mix + wet
    return mix


def seq(text):
    return text.split()


def music():
    # --- Stage theme: A minor, driving. Progression Am F C G (x2) + Dm Am E E
    bass = seq("A2 . A3 . A2 . A3 . A2 . A3 . G2 . G3 .  F2 . F3 . F2 . F3 . F2 . F3 . E2 . E3 . "
               "C3 . C4 . C3 . C4 . C3 . C4 . B2 . C3 .  G2 . G3 . G2 . G3 . G2 . G3 . A2 . B2 . "
               "A2 . A3 . A2 . A3 . A2 . A3 . G2 . G3 .  F2 . F3 . F2 . F3 . F2 . F3 . E2 . E3 . "
               "D3 . D4 . D3 . D4 . A2 . A3 . A2 . A3 .  E2 . E3 . E2 . E3 . E2 . E3 . G#2 . B2 .")
    lead = seq("A4 . . . C5 . E5 . . . D5 . C5 . B4 .  C5 . . . A4 . . . - - F4 . A4 . C5 . "
               "E5 . . . G5 . E5 . . . D5 . C5 . D5 .  B4 . . . G4 . . . - - B4 . C5 . D5 . "
               "E5 . . . A5 . G5 . . . E5 . D5 . C5 .  A4 . . . C5 . . . - - A4 . C5 . F5 . "
               "F5 . E5 . D5 . . . C5 . B4 . A4 . . .  B4 . . . . . G#4 . E4 . . . - - - - ")
    harm = seq("E4 . . . A4 . C5 . . . B4 . A4 . G4 .  A4 . . . F4 . . . - - C4 . F4 . A4 . "
               "C5 . . . E5 . C5 . . . B4 . A4 . B4 .  G4 . . . D4 . . . - - G4 . A4 . B4 . "
               "C5 . . . E5 . E5 . . . C5 . B4 . A4 .  F4 . . . A4 . . . - - F4 . A4 . C5 . "
               "D5 . C5 . A4 . . . A4 . G4 . F4 . . .  G#4 . . . . . E4 . B3 . . . - - - - ")
    kick = seq("C . - - - - - - C . C . - - - - ")
    snare = seq("- - - - S . - - - - - - S . - S ")
    hat = seq("- - H - - - H - - - H - - - H H ")
    stage = render_track(150, [
        ("tri", bass, 0.55, 0), ("pulse", lead, 0.22, 0.25), ("pulse", harm, 0.11, 0.125),
        ("kick", kick, 0.7, 0), ("snare", snare, 0.35, 0), ("hat", hat, 0.12, 0)], bars=8)
    write("music_stage.wav", np.tile(stage, 2), 0.8, loop=True)

    # --- Boss theme: E phrygian-ish, faster, ostinato bass.
    bass = seq("E2 . E3 . E2 . E3 . F2 . F3 . E2 . E3 .  E2 . E3 . E2 . E3 . G2 . G3 . F2 . F3 . "
               "C3 . C4 . C3 . C4 . B2 . B3 . A2 . A3 .  B2 . B3 . B2 . B3 . F2 . F3 . B2 . D3 .")
    lead = seq("E5 . . . . . F5 . E5 . D5 . E5 . . .  G5 . . . F5 . E5 . F5 . . . - - D5 . "
               "C5 . . . E5 . . . D5 . C5 . B4 . A4 .  B4 . . . . . C5 . D5 . . . F5 . E5 . ")
    harm = seq("B4 . . . . . C5 . B4 . A4 . B4 . . .  D5 . . . C5 . B4 . C5 . . . - - A4 . "
               "G4 . . . C5 . . . A4 . G4 . F4 . E4 .  F4 . . . . . A4 . B4 . . . D5 . B4 . ")
    kick = seq("C . - - C . - - C . - - C . C . ")
    snare = seq("- - - - S . - - - - - - S . - - ")
    hat = seq("H - H - H - H - H - H - H - H H ")
    boss = render_track(172, [
        ("tri", bass, 0.6, 0), ("pulse", lead, 0.22, 0.5), ("pulse", harm, 0.1, 0.25),
        ("kick", kick, 0.75, 0), ("snare", snare, 0.4, 0), ("hat", hat, 0.1, 0)], bars=4)
    write("music_boss.wav", np.tile(boss, 4), 0.8, loop=True)

    # --- Title theme: C major anthem, mid tempo. C Am F G.
    bass = seq("C3 . . . G2 . . . C3 . . . G2 . . .  A2 . . . E2 . . . A2 . . . E2 . . . "
               "F2 . . . C3 . . . F2 . . . C3 . . .  G2 . . . D3 . . . G2 . . . B2 . . .")
    lead = seq("G4 . . . C5 . . . E5 . . . D5 . C5 .  E5 . . . . . . . C5 . . . A4 . . . "
               "F4 . . . A4 . C5 . F5 . . . E5 . D5 .  D5 . . . . . . . - - G4 . B4 . D5 . "
               "E5 . . . G5 . . . E5 . . . D5 . C5 .  A4 . . . . . . . C5 . . . E5 . . . "
               "F5 . . . E5 . D5 . C5 . . . A4 . C5 .  D5 . . . . . . . . . . . - - - - ")
    arp = seq("C4 E4 G4 E4 C4 E4 G4 E4 C4 E4 G4 E4 C4 E4 G4 E4  A3 C4 E4 C4 A3 C4 E4 C4 A3 C4 E4 C4 A3 C4 E4 C4 "
              "F3 A3 C4 A3 F3 A3 C4 A3 F3 A3 C4 A3 F3 A3 C4 A3  G3 B3 D4 B3 G3 B3 D4 B3 G3 B3 D4 B3 G3 B3 D4 B3")
    kick = seq("C . - - - - - - C . - - - - - - ")
    snare = seq("- - - - S . - - - - - - S . - - ")
    hat = seq("- - H - - - H - - - H - - - H - ")
    title = render_track(118, [
        ("tri", bass, 0.55, 0), ("pulse", lead, 0.22, 0.25), ("pulse", arp, 0.07, 0.125),
        ("kick", kick, 0.6, 0), ("snare", snare, 0.3, 0), ("hat", hat, 0.1, 0)], bars=8)
    write("music_title.wav", title, 0.8, loop=True)

    # --- Jingles (no loop).
    clear = render_track(140, [
        ("pulse", seq("C5 . E5 . G5 . C6 . . . G5 . C6 . . . . . . . - - - - - - - - - - - - "), 0.3, 0.25),
        ("pulse", seq("E4 . G4 . C5 . E5 . . . E5 . G5 . . . . . . . - - - - - - - - - - - - "), 0.15, 0.125),
        ("tri", seq("C3 . . . . . . . G2 . . . C3 . . . . . . . - - - - - - - - - - - - "), 0.5, 0),
        ("kick", seq("C . - - - - - - C . - - C . - - - - - - - - - - - - - - - - - - "), 0.6, 0)], bars=2, loop=False)
    write("jingle_clear.wav", clear, 0.8)

    over = render_track(100, [
        ("pulse", seq("E5 . . . D#5 . . . D5 . . . C#5 . . . . . . . . . . . - - - - - - - - "), 0.3, 0.5),
        ("tri", seq("A3 . . . G#3 . . . G3 . . . F#3 . . . . . . . . . . . - - - - - - - - "), 0.5, 0)],
        bars=2, loop=False)
    write("jingle_gameover.wav", over, 0.8)

    win = render_track(132, [
        ("pulse", seq("G4 . G4 . G4 . C5 . . . . . G4 . C5 . E5 . . . . . C5 . E5 . G5 . . . . . . . "
                      "E5 . G5 . C6 . . . . . . . . . . . - - - - - - - - - - - - - - - - "), 0.3, 0.25),
        ("pulse", seq("E4 . E4 . E4 . E4 . . . . . E4 . G4 . C5 . . . . . G4 . C5 . E5 . . . . . . . "
                      "C5 . E5 . G5 . . . . . . . . . . . - - - - - - - - - - - - - - - - "), 0.14, 0.125),
        ("tri", seq("C3 . . . . . . . . . . . G2 . . . C3 . . . . . . . E3 . . . G3 . . . "
                    "C3 . . . . . . . . . . . - - - - - - - - - - - - - - - - - - - - "), 0.5, 0),
        ("kick", seq("C . - - - - - - C . - - - - - - "), 0.6, 0)], bars=4, loop=False)
    write("jingle_victory.wav", win, 0.8)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    sfx()
    music()
