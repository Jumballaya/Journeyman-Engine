#!/usr/bin/env python3
"""Measures the engine, so a change can be judged by its before and after.

  scripts/bench.py ab --before DIR [--after DIR]   before vs after, alternating: decide with this
  scripts/bench.py run <label> [--tree DIR]        record bench/results/<label>.json (a milestone)
  scripts/bench.py compare <before> <after>        compare two recordings

Machine speed drifts by 10-15% over hours (heat, background work), more than
many changes are worth, so recordings from different times don't compare
well. `ab` builds both trees and alternates every round between them (A B,
B A, ...), so drift falls on both alike:

  git worktree add /tmp/before HEAD        # the code before the change
  scripts/bench.py ab --before /tmp/before # vs this checkout, as edited

Record with `run` once a change is finished and committed.

Trees are built optimized once; then each round runs
  - micro-benchmarks: the disabled *Cost tests (BENCH lines), where a tree has them
  - per demo: ms/frame over a fixed replay (wall, and CPU: the steadier), peak memory, startup time
  - bench/glyph_stress: peak memory with world text growing a pixel a frame
  - bench/sprite_stress: ms/frame for 4000 sprites alternating two textures
and every number is the median of its rounds. The replay and stress projects
always come from this checkout, so old commits are measured like new ones.
"""
import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent  # this checkout: the harness, replay and stress projects
RESULTS = HERE / "bench" / "results"
REPLAY = HERE / "bench" / "replay.txt"
DEMOS = ["strike_wing", "jrpg", "dungeon", "platformer", "tetris", "Ash and Iron"]
FRAMES = 2000
ROUNDS = 5
MICRO = [  # (test binary under the bench build, gtest filter)
    ("engine/physics2d/tests/test_engine_physics2d", "*CollisionCost*:*MoveCost*"),
    ("engine/core/tests/test_engine_core", "*EcsCost*"),
    ("engine/ui/tests/test_engine_ui", "*FlexDepthCost*:*DemoScreensCost*"),
]


def sh(cmd, cwd=None, env=None, check=True):
    """Runs a command, returning its combined output."""
    result = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if check and result.returncode != 0:
        sys.exit(f"bench: {' '.join(map(str, cmd))} failed:\n{result.stdout[-3000:]}")
    return result.stdout


def game_env(save_dir, **extra):
    env = dict(os.environ, JM_HEADLESS="1", JM_FIXED_DT="0.0166667", JM_SEED="1", JM_SAVE_DIR=str(save_dir))
    env.update({k: str(v) for k, v in extra.items()})
    return env


def timed(cmd, cwd, env):
    """Runs a command under time(1): (CPU seconds, user + system; peak resident memory, MB).
    CPU time leaves out waiting on the GPU and the scheduler, so it varies far
    less between runs than wall time does."""
    if sys.platform == "darwin":
        out = sh(["/usr/bin/time", "-l", *cmd], cwd=cwd, env=env, check=False)
        user, system = re.search(r"([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys", out).group(2, 3)
        memory = int(re.search(r"(\d+)\s+maximum resident set size", out).group(1)) / 2**20
    else:
        out = sh(["/usr/bin/time", "-v", *cmd], cwd=cwd, env=env, check=False)
        user = re.search(r"User time \(seconds\): ([\d.]+)", out).group(1)
        system = re.search(r"System time \(seconds\): ([\d.]+)", out).group(1)
        memory = int(re.search(r"Maximum resident set size \(kbytes\): (\d+)", out).group(1)) / 2**10
    return float(user) + float(system), memory


def last_frame_ms(build_dir):
    log = (build_dir / "logs/engine.log").read_text(errors="replace")
    return float(re.findall(r"\(([\d.]+) ms/frame avg\)", log)[-1])


def sprite_scene(path, alternate=True):
    """4000 sprites in depth order alternating two textures: the renderer's
    worst case, a new texture run for every sprite. Without alternating, one
    texture: the best case, one run."""
    entities = []
    for i in range(4000):
        texture = "assets/textures/red.png" if i % 2 == 0 or not alternate else "assets/textures/blue.png"
        entities.append({"name": f"s{i}", "components": {
            "TransformComponent": {"position": [(i % 80) * 16 - 632, (i // 80) * 14 - 350, i * 0.01], "scale": [8, 8]},
            "SpriteComponent": {"texture": texture}}})
    path.write_text(json.dumps({"name": "main", "entities": entities}))


SCRIPTED = 1000


def script_scene(path):
    """1000 entities, each with its own small script running every frame."""
    entities = [{"name": f"s{i}", "components": {
        "TransformComponent": {"position": [(i % 40) * 30 - 600, (i // 40) * 25 - 300, 0]},
        "ScriptComponent": {"script": "assets/scripts/bob.ts"}}} for i in range(SCRIPTED)]
    path.write_text(json.dumps({"name": "main", "entities": entities}))


class Build:
    """One source tree, built optimized, with its demos and the stress projects built by its jm."""

    def __init__(self, name, tree, work):
        self.name, self.tree = name, Path(tree).resolve()
        self.work = work / name
        self.work.mkdir(parents=True)
        print(f"Building {name}: {self.tree} (optimized)...", flush=True)
        sh(["cmake", "--preset", "release", "-DJM_BUILD_EDITOR=OFF"], cwd=self.tree)
        sh(["cmake", "--build", "--preset", "release", "--target", "journeyman_engine"], cwd=self.tree)
        sh(["cmake", "--preset", "tests", "-B", "build/bench", "-DCMAKE_BUILD_TYPE=Release", "-DJM_BUILD_EDITOR=OFF"],
           cwd=self.tree)
        sh(["cmake", "--build", "build/bench"], cwd=self.tree)
        self.engine = self.tree / "build/release/engine/journeyman_engine"
        jm = self.work / "jm"
        sh(["go", "build", "-o", str(jm), "./cmd/jm"], cwd=self.tree / "cli")
        self.demos = [self.tree / "demos" / d for d in DEMOS if (self.tree / "demos" / d / ".jm.json").exists()]
        for game in self.demos:
            sh([str(jm), "build"], cwd=game)
        self.glyphs = self._stress_project("glyph_stress", jm)
        self.sprites = self._stress_project("sprite_stress", jm, write_scene=sprite_scene)
        self.batched = self._stress_project("sprite_stress", jm, write_scene=lambda p: sprite_scene(p, alternate=False),
                                            name="sprite_batched")
        self.scripted = (self._stress_project("script_stress", jm, write_scene=script_scene)
                         if (HERE / "bench" / "script_stress").exists() else None)
        self.commit = sh(["git", "rev-parse", "--short", "HEAD"], cwd=self.tree).strip()

    def _stress_project(self, source, jm, write_scene=None, name=None):
        project = self.work / (name or source)
        shutil.copytree(HERE / "bench" / source, project)
        if write_scene:
            write_scene(project / "scenes/main.scene.json")
        sh([str(jm), "build"], cwd=project)
        return project / "build"

    def round(self, n):
        """Every measurement once: {name: (value, unit)}."""
        out = {}
        for binary, pattern in MICRO:
            path = self.tree / "build/bench" / binary
            if path.exists():
                text = sh([str(path), "--gtest_also_run_disabled_tests", f"--gtest_filter={pattern}"], check=False)
                for name, value, unit in re.findall(r"^BENCH (\S+) ([\d.]+) (\S+)$", text, re.M):
                    out[name] = (float(value), unit)
        engine = str(self.engine)
        for game in self.demos:
            key = game.name.replace(" ", "_").lower()
            build_dir, save = game / "build", self.work / f"save-{key}-{n}"
            env = game_env(save, JM_EXIT_AFTER_FRAMES=FRAMES, JM_INPUT_REPLAY=REPLAY)
            cpu, memory = timed([engine, "."], build_dir, env)
            out[f"{key}_peak_memory"] = (memory, "MB")
            out[f"{key}_frame"] = (last_frame_ms(build_dir), "ms")
            out[f"{key}_cpu_per_frame"] = (cpu * 1000 / FRAMES, "ms")
            start = time.perf_counter()
            sh([engine, "."], cwd=build_dir, env=game_env(save, JM_EXIT_AFTER_FRAMES=1), check=False)
            out[f"{key}_startup"] = ((time.perf_counter() - start) * 1000, "ms")
        env = game_env(self.work / f"save-glyphs-{n}", JM_EXIT_AFTER_FRAMES=1300)
        out["glyph_stress_peak_memory"] = (timed([engine, "."], self.glyphs, env)[1], "MB")
        env = game_env(self.work / f"save-sprites-{n}", JM_EXIT_AFTER_FRAMES=600)
        cpu, _ = timed([engine, "."], self.sprites, env)
        out["sprite_stress_frame"] = (last_frame_ms(self.sprites), "ms")
        out["sprite_stress_cpu_per_frame"] = (cpu * 1000 / 600, "ms")
        env = game_env(self.work / f"save-batched-{n}", JM_EXIT_AFTER_FRAMES=600)
        cpu, _ = timed([engine, "."], self.batched, env)
        out["sprite_batched_cpu_per_frame"] = (cpu * 1000 / 600, "ms")
        if self.scripted:
            # Two lengths: the difference is the frames' cost alone, without startup
            # (each scripted entity's start is most of a short run).
            costs = []
            for frames in (60, 360):
                env = game_env(self.work / f"save-scripts-{n}-{frames}", JM_EXIT_AFTER_FRAMES=frames)
                cpu, memory = timed([engine, "."], self.scripted, env)
                costs.append(cpu)
            out["script_1000_entities_peak_memory"] = (memory, "MB")
            out["script_1000_entities_cpu_per_frame"] = ((costs[1] - costs[0]) * 1000 / 300, "ms")
            out["script_1000_entities_startup_cpu"] = ((costs[0] - (costs[1] - costs[0]) / 5) * 1000, "ms")
        return out


def measure(builds, rounds=ROUNDS):
    """Rounds alternate between builds (A B, B A, ...); each number is the median of its rounds."""
    samples = {b.name: {} for b in builds}
    for n in range(rounds):
        for b in builds if n % 2 == 0 else list(reversed(builds)):
            print(f"Round {n + 1}/{rounds}: {b.name}", flush=True)
            for name, (value, unit) in b.round(n).items():
                samples[b.name].setdefault(name, ([], unit))[0].append(value)
    return {b.name: record(b, samples[b.name]) for b in builds}


def record(build, samples):
    return {
        "label": build.name,
        "commit": build.commit,
        "date": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "machine": {"os": platform.platform(), "cpu": platform.processor() or platform.machine()},
        "results": {k: {"value": statistics.median(v), "unit": u} for k, (v, u) in sorted(samples.items())},
    }


def show(before, after):
    if before["machine"] != after["machine"]:
        print("warning: measured on different machines; the numbers aren't comparable\n")
    print(f"\n{before['label']} ({before['commit']}) -> {after['label']} ({after['commit']})\n")
    names = sorted(set(before["results"]) | set(after["results"]))
    width = max(map(len, names), default=10)
    print(f"{'measurement':<{width}}  {'before':>12}  {'after':>12}  change")
    for name in names:
        b, a = before["results"].get(name), after["results"].get(name)
        fmt = lambda r: f"{r['value']:.3f} {r['unit']}" if r else "-"
        change = ""
        if a and b and b["value"]:
            ratio = a["value"] / b["value"]
            change = f"{(ratio - 1) * 100:+.1f}%" + (f"  ({1 / ratio:.1f}x less)" if ratio < 0.5 else "")
        print(f"{name:<{width}}  {fmt(b):>12}  {fmt(a):>12}  {change}")


def load(label):
    path = Path(label) if label.endswith(".json") else RESULTS / f"{label}.json"
    return json.loads(path.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    ab = sub.add_parser("ab", help="before vs after, alternating rounds (prints, saves nothing)")
    ab.add_argument("--before", required=True, help="the tree before the change (a git worktree)")
    ab.add_argument("--after", default=str(HERE), help="the tree after (default: this checkout)")
    ab.add_argument("--rounds", type=int, default=ROUNDS)
    r = sub.add_parser("run", help="record bench/results/<label>.json")
    r.add_argument("label")
    r.add_argument("--tree", default=str(HERE))
    c = sub.add_parser("compare", help="compare two recordings")
    c.add_argument("before")
    c.add_argument("after")
    args = parser.parse_args()

    if args.command == "compare":
        show(load(args.before), load(args.after))
        return
    work = Path(tempfile.mkdtemp())
    try:
        if args.command == "ab":
            builds = [Build("before", args.before, work), Build("after", args.after, work)]
            results = measure(builds, args.rounds)
            show(results["before"], results["after"])
        else:
            result = measure([Build(args.label, args.tree, work)])[args.label]
            RESULTS.mkdir(parents=True, exist_ok=True)
            path = RESULTS / f"{args.label}.json"
            path.write_text(json.dumps(result, indent=2) + "\n")
            print(f"Wrote {path.relative_to(HERE)} ({len(result['results'])} measurements)")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
