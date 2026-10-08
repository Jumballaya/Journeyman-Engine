#!/usr/bin/env python3
"""Measures the engine and records the numbers, so a change can be judged by
its before and after.

  scripts/bench.py run <label> [--tree DIR]    measure, write bench/results/<label>.json
  scripts/bench.py run <label> --no-save --compare <baseline>
                                               measure while working: compare, save nothing
  scripts/bench.py compare <before> <after>    compare two result files

Record (save) after a change is finished and committed; while working on it,
--no-save --compare shows where it stands against the last recording.

`run` builds DIR (default: this checkout) optimized, then records:
  - micro-benchmarks: the disabled *Cost tests (BENCH lines), where DIR has them
  - per demo: ms/frame over a fixed replay, peak memory, startup time
  - the glyph stress project (bench/glyph_stress): peak memory
It always uses this checkout's replay and stress project, so an older commit
(checked out with `git worktree add`) is measured exactly like the current one:

  git worktree add /tmp/before <commit>
  scripts/bench.py run before --tree /tmp/before
  scripts/bench.py run after
  scripts/bench.py compare before after

Numbers are only comparable from the same machine; results record which.
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

HERE = Path(__file__).resolve().parent.parent  # this checkout: the harness, replay and stress project
RESULTS = HERE / "bench" / "results"
REPLAY = HERE / "bench" / "replay.txt"
DEMOS = ["strike_wing", "jrpg", "dungeon", "platformer", "tetris", "Ash and Iron"]
FRAMES = 2000
RUNS = 3  # each engine measurement is the median of this many runs
MICRO = [  # (test binary under the bench build, gtest filter)
    ("engine/physics2d/tests/test_engine_physics2d", "*CollisionCost*"),
    ("engine/core/tests/test_engine_core", "*EcsCost*"),
    ("engine/ui/tests/test_engine_ui", "*FlexDepthCost*"),
]


def sh(cmd, cwd=None, env=None, check=True):
    """Runs a command, returning its combined output."""
    result = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if check and result.returncode != 0:
        sys.exit(f"bench: {' '.join(map(str, cmd))} failed:\n{result.stdout[-3000:]}")
    return result.stdout


def build(tree):
    print(f"Building {tree} (optimized)...", flush=True)
    sh(["cmake", "--preset", "release", "-DJM_BUILD_EDITOR=OFF"], cwd=tree)
    sh(["cmake", "--build", "--preset", "release", "--target", "journeyman_engine"], cwd=tree)
    sh(["cmake", "--preset", "tests", "-B", "build/bench", "-DCMAKE_BUILD_TYPE=Release", "-DJM_BUILD_EDITOR=OFF"], cwd=tree)
    sh(["cmake", "--build", "build/bench"], cwd=tree)
    jm = Path(tempfile.mkdtemp()) / "jm"
    sh(["go", "build", "-o", str(jm), "./cmd/jm"], cwd=tree / "cli")
    return tree / "build/release/engine/journeyman_engine", jm


def micro(tree):
    results = {}
    for binary, pattern in MICRO:
        path = tree / "build/bench" / binary
        if not path.exists():
            continue
        out = sh([str(path), "--gtest_also_run_disabled_tests", f"--gtest_filter={pattern}"], check=False)
        for name, value, unit in re.findall(r"^BENCH (\S+) ([\d.]+) (\S+)$", out, re.M):
            results[name] = {"value": float(value), "unit": unit}
    return results


def peak_memory_mb(cmd, cwd, env):
    """Runs a command under time(1); its peak resident memory in MB."""
    if sys.platform == "darwin":
        out = sh(["/usr/bin/time", "-l", *cmd], cwd=cwd, env=env, check=False)
        return int(re.search(r"(\d+)\s+maximum resident set size", out).group(1)) / 2**20
    out = sh(["/usr/bin/time", "-v", *cmd], cwd=cwd, env=env, check=False)
    return int(re.search(r"Maximum resident set size \(kbytes\): (\d+)", out).group(1)) / 2**10


def game_env(save_dir, **extra):
    env = dict(os.environ, JM_HEADLESS="1", JM_FIXED_DT="0.0166667", JM_SEED="1", JM_SAVE_DIR=str(save_dir))
    env.update({k: str(v) for k, v in extra.items()})
    return env


def demo(engine, jm, game, work):
    """ms/frame, peak memory and startup of one demo, each the median of RUNS."""
    sh([str(jm), "build"], cwd=game)
    build_dir = game / "build"
    frame_ms, memory, startup = [], [], []
    for run in range(RUNS):
        save = work / f"save-{game.name}-{run}"
        env = game_env(save, JM_EXIT_AFTER_FRAMES=FRAMES, JM_INPUT_REPLAY=REPLAY)
        memory.append(peak_memory_mb([str(engine), "."], build_dir, env))
        log = (build_dir / "logs/engine.log").read_text(errors="replace")
        frame_ms.append(float(re.findall(r"\(([\d.]+) ms/frame avg\)", log)[-1]))
        start = time.perf_counter()
        sh([str(engine), "."], cwd=build_dir, env=game_env(save, JM_EXIT_AFTER_FRAMES=1), check=False)
        startup.append((time.perf_counter() - start) * 1000)
    key = game.name.replace(" ", "_").lower()
    return {
        f"{key}_frame": {"value": statistics.median(frame_ms), "unit": "ms"},
        f"{key}_peak_memory": {"value": statistics.median(memory), "unit": "MB"},
        f"{key}_startup": {"value": statistics.median(startup), "unit": "ms"},
    }


def glyph_stress(engine, jm, work):
    """Peak memory of world text growing a pixel each frame for 1300 frames."""
    project = work / "glyph_stress"
    shutil.copytree(HERE / "bench/glyph_stress", project)
    sh([str(jm), "build"], cwd=project)
    env = game_env(work / "save-glyphs", JM_EXIT_AFTER_FRAMES=1300)
    return {"glyph_stress_peak_memory": {"value": peak_memory_mb([str(engine), "."], project / "build", env), "unit": "MB"}}


def run(label, tree, save=True, baseline=None):
    tree = Path(tree).resolve()
    engine, jm = build(tree)
    work = Path(tempfile.mkdtemp())
    results = micro(tree)
    for name in DEMOS:
        if (tree / "demos" / name / ".jm.json").exists():
            print(f"Measuring {name}...", flush=True)
            results.update(demo(engine, jm, tree / "demos" / name, work))
    print("Measuring the glyph stress project...", flush=True)
    results.update(glyph_stress(engine, jm, work))
    shutil.rmtree(work, ignore_errors=True)

    record = {
        "label": label,
        "commit": sh(["git", "rev-parse", "--short", "HEAD"], cwd=tree).strip(),
        "date": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "machine": {"os": platform.platform(), "cpu": platform.processor() or platform.machine()},
        "results": dict(sorted(results.items())),
    }
    if save:
        RESULTS.mkdir(parents=True, exist_ok=True)
        path = RESULTS / f"{label}.json"
        path.write_text(json.dumps(record, indent=2) + "\n")
        print(f"Wrote {path.relative_to(HERE)} ({len(results)} measurements)")
    if baseline:
        print()
        show(load(baseline), record)


def load(label):
    path = Path(label) if label.endswith(".json") else RESULTS / f"{label}.json"
    return json.loads(path.read_text())


def compare(before_label, after_label):
    show(load(before_label), load(after_label))


def show(before, after):
    if before["machine"] != after["machine"]:
        print("warning: measured on different machines; the numbers aren't comparable\n")
    print(f"{before['label']} ({before['commit']}) -> {after['label']} ({after['commit']})\n")
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="measure and record")
    r.add_argument("label")
    r.add_argument("--tree", default=str(HERE), help="the source tree to build and measure")
    r.add_argument("--no-save", action="store_true", help="don't write bench/results/<label>.json")
    r.add_argument("--compare", metavar="BASELINE", help="compare against a recorded run")
    c = sub.add_parser("compare", help="compare two recorded runs")
    c.add_argument("before")
    c.add_argument("after")
    args = parser.parse_args()
    if args.command == "run":
        run(args.label, args.tree, save=not args.no_save, baseline=args.compare)
    else:
        compare(args.before, args.after)


if __name__ == "__main__":
    main()
