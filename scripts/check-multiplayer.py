#!/usr/bin/env python3
"""Plays a multiplayer demo's session on this machine and checks every peer
agrees: `jm run --peers N` with no window or GPU (JM_RENDERER=none), each
game replaying tests/multiplayer/peer<N>.txt, then each one's state dump at
the frame tests/multiplayer.json names must show the session connected, the
same scene, and the shared entities it lists (by tag, how many).

  scripts/check-multiplayer.py <jm> <demo folder>

tests/multiplayer.json: {"peers": 2, "frames": 640, "dump": 620,
  "scene": "scenes/board.scene.json", "tags": {"tank": 2}, "roles": ["host", "client"]}
("roles", optional: each peer's, in order.) Needs the demo built (jm build).
"""
import json
import os
import subprocess
import sys
import tempfile

jm, demo = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
spec = json.load(open(os.path.join(demo, "tests", "multiplayer.json")))
peers = spec["peers"]

with tempfile.TemporaryDirectory() as work:
    env = dict(os.environ,
               JM_RENDERER="none", JM_REALTIME="1",
               JM_EXIT_AFTER_FRAMES=str(spec["frames"]),
               JM_DUMP_DIR=os.path.join(work, "dump"), JM_DUMP_FRAMES=str(spec["dump"]),
               JM_ERRORS=os.path.join(work, "errors.jsonl"),
               JM_SAVE_DIR=os.path.join(work, "save"),
               JM_INPUT_REPLAY=os.path.join(demo, "tests", "multiplayer", "{peer}.txt"))
    run = subprocess.run([jm, "run", "--peers", str(peers)], cwd=demo, env=env, timeout=spec["frames"] / 60 + 60,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    failures = []
    if run.returncode != 0:
        failures.append(f"jm run --peers exited {run.returncode}")
    for name in os.listdir(work):
        if name.startswith("errors") and os.path.getsize(os.path.join(work, name)) > 0:
            failures.append(f"{name}: " + open(os.path.join(work, name)).read().strip())

    for i in range(1, peers + 1):
        label = f"peer{i}"
        path = os.path.join(work, "dump", label, f"state_{spec['dump']:05d}.json")
        if not os.path.exists(path):
            failures.append(f"{label}: no state dump at frame {spec['dump']}")
            continue
        state = json.load(open(path))
        net = state.get("net", {})
        if net.get("status") != 2:
            failures.append(f"{label}: not connected (status {net.get('status')}, error {net.get('error')!r})")
        if "roles" in spec and net.get("role") != spec["roles"][i - 1]:
            failures.append(f"{label}: role {net.get('role')}, expected {spec['roles'][i - 1]}")
        if state.get("scene") != spec["scene"]:
            failures.append(f"{label}: in {state.get('scene')}, expected {spec['scene']}")
        for tag, count in spec.get("tags", {}).items():
            shared = [e for e in state["entities"]
                      if tag in e.get("tags", []) and "NetworkComponent" in e.get("components", {})]
            if len(shared) != count:
                failures.append(f"{label}: {len(shared)} shared '{tag}' entities, expected {count}")
        print(f"{label}: {net.get('role')} player {net.get('player')} in {state.get('scene')}, "
              f"{len(net.get('entities', []))} shared entities")

    if failures:
        print(run.stdout[-4000:])
        print("FAIL:\n  " + "\n  ".join(failures))
        sys.exit(1)
    print(f"OK: {peers} peers agree")
