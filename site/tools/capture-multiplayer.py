#!/usr/bin/env python3
"""Build, play and capture the multiplayer examples. Run after building the release engine.

Usage: python3 site/tools/capture-multiplayer.py [pellet_party|tank_arena|checkers]
Needs a display for OpenGL and Pillow. Raw frames, logs and state dumps stay in
build/site-multiplayer; site JPEGs include 640px variants.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
JM = REPO / "build/bin/jm"
ENV = dict(os.environ,
           JM_ENGINE=str(REPO / "build/release/engine/journeyman_engine"),
           JM_SERVER=str(REPO / "build/release/engine/journeyman_server"),
           JM_HEADLESS="1", JM_REALTIME="1", JM_SEED="1", JM_STRICT="1")
DEMOS = {
    "pellet_party": ("pellet-party", [400, 240, 160, 620]),
    "tank_arena": ("tank-arena", [400, 240, 160, 620]),
    "checkers": ("checkers", [380, 402, 420, 550]),
}


def capture(demo):
    slug, frames = DEMOS[demo]
    project = REPO / "demos" / demo
    work = REPO / "build/site-multiplayer" / demo
    work.mkdir(parents=True, exist_ok=True)
    spec = json.loads((project / "tests/multiplayer.json").read_text())

    def run(label, args, **env):
        with (work / f"{label}.log").open("w") as log:
            subprocess.run([str(JM), *args], cwd=project,
                           env=dict(ENV, JM_SAVE_DIR=str(work / "save"), **env),
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)

    run("build", ["build"])
    run("title", ["run"], JM_EXIT_AFTER_FRAMES="61",
        JM_CAPTURE_DIR=str(work / "title"), JM_CAPTURE_FRAMES="60")
    run("session", ["run", "--peers", str(spec["peers"])],
        JM_EXIT_AFTER_FRAMES="640", JM_CAPTURE_DIR=str(work / "session"),
        JM_CAPTURE_FRAMES=",".join(map(str, frames)),
        JM_DUMP_DIR=str(work / "dump"), JM_DUMP_FRAMES=str(spec["dump"]),
        JM_ERRORS=str(work / "errors.jsonl"),
        JM_INPUT_REPLAY=str(project / "tests/multiplayer/{peer}.txt"))

    for errors in work.glob("errors*.jsonl"):
        if errors.read_text().strip():
            raise RuntimeError(f"Engine errors: {errors}")
    screenshot_peer = 1
    for peer in range(1, spec["peers"] + 1):
        state = json.loads((work / f"dump/peer{peer}/state_{spec['dump']:05d}.json").read_text())
        assert state["net"]["status"] == 2, f"{demo} peer{peer} disconnected"
        assert state["scene"] == spec["scene"], f"{demo} peer{peer} wrong scene"
        if "roles" in spec:
            assert state["net"]["role"] == spec["roles"][peer - 1]
        if state["net"]["role"] == "host":
            screenshot_peer = peer
        for tag, count in spec["tags"].items():
            entities = [e for e in state["entities"] if tag in e.get("tags", [])
                        and "NetworkComponent" in e.get("components", {})]
            assert len(entities) == count, f"{demo} peer{peer}: shared {tag} count"
        print(f"{demo} peer{peer}: {state['net']['role']}, connected in {state['scene']}")

    out = REPO / "site/src/img/games" / slug
    out.mkdir(parents=True, exist_ok=True)
    images = [("title", work / "title/frame_00060.png")]
    images += [(str(i), work / f"session/peer{screenshot_peer}/frame_{frame:05d}.png")
               for i, frame in enumerate(frames, 1)]
    for name, source in images:
        with Image.open(source) as raw:
            image = raw.convert("RGB")
            image.save(out / f"{name}.jpg", quality=88)
            image.resize((640, round(image.height * 640 / image.width)), Image.Resampling.LANCZOS).save(
                out / f"{name}-640.jpg", quality=88)
    print(f"{demo}: wrote title and four gameplay screenshots to {out}")


for demo in sys.argv[1:] or DEMOS:
    capture(demo)
