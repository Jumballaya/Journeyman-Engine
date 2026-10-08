#!/usr/bin/env bash
# Plays a built game twice from the same replay, once with OpenGL and once
# with no window or GL at all (JM_RENDERER=none, no display), both strict
# (JM_STRICT: any error fails), and fails unless their state dumps agree:
# the same entities, components, UI and draw list. Screen quads' sizes are
# left out, since text is rasterized at the display's pixel scale.
#
#   scripts/check-null-renderer.sh <build folder> <replay file> [frames] [engine]
#     frames: comma-separated frame numbers to dump (default 300,900)
#
# The GL run needs a display (xvfb-run on Linux); the null run is given none.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
game="$(cd "$1" && pwd)"
replay="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
frames="${3:-300,900}"
engine="${4:-$root/build/release/engine/journeyman_engine}"
last="$(tr ',' '\n' <<<"$frames" | sort -n | tail -1)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

run() {  # run <name> [env options and VAR=value...]
  local name="$1"; shift
  (cd "$game" && env "$@" JM_HEADLESS=1 JM_STRICT=1 JM_ERRORS=- JM_EXIT_AFTER_FRAMES=$((last + 1)) \
    JM_INPUT_REPLAY="$replay" JM_SAVE_DIR="$work/save-$name" JM_DUMP_DIR="$work/$name" JM_DUMP_FRAMES="$frames" \
    "$engine" . >"$work/log-$name.txt" 2>&1) || { cat "$work/log-$name.txt"; echo "FAIL: the $name run failed"; exit 1; }
}
run gl
run none -u DISPLAY -u WAYLAND_DISPLAY JM_RENDERER=none

python3 - "$work" "$frames" <<'PY'
import json, sys
work, frames = sys.argv[1], sys.argv[2].split(",")
failed = False
for frame in frames:
    name = "state_%05d.json" % int(frame)
    dumps = []
    for run in ("gl", "none"):
        state = json.load(open(f"{work}/{run}/{name}"))
        screen = state["draw"].pop("screen")
        state["draw"]["screenCount"] = len(screen)
        dumps.append(state)
    if dumps[0] == dumps[1]:
        print(f"frame {frame}: same state with and without GL")
    else:
        failed = True
        keys = [k for k in dumps[0] if dumps[0].get(k) != dumps[1].get(k)]
        print(f"FAIL: frame {frame}: the runs differ in {', '.join(keys)}")
sys.exit(1 if failed else 0)
PY
