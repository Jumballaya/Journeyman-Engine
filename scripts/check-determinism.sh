#!/usr/bin/env bash
# Plays a built game twice from the same input replay and fails unless every
# captured frame is byte-identical: the engine's determinism contract (fixed
# step, seeded randomness, ordered single-threaded frames). Each run gets a
# fresh save folder, so nothing the first run saved changes the second.
#
#   scripts/check-determinism.sh <build folder> <replay file> [frames] [engine]
#     frames: comma-separated frame numbers to capture (default 60,300,900)
#     engine: the engine binary (default build/release/engine/journeyman_engine)
#
# Needs a display or a virtual one (xvfb-run on Linux).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
game="$(cd "$1" && pwd)"
replay="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
frames="${3:-60,300,900}"
engine="${4:-$root/build/release/engine/journeyman_engine}"
last="$(tr ',' '\n' <<<"$frames" | sort -n | tail -1)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for run in 1 2; do
  (cd "$game" && JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=$((last + 1)) JM_INPUT_REPLAY="$replay" \
    JM_SAVE_DIR="$work/save$run" JM_CAPTURE_DIR="$work/run$run" JM_CAPTURE_FRAMES="$frames" \
    "$engine" . >"$work/log$run.txt" 2>&1) || { cat "$work/log$run.txt"; echo "FAIL: run $run exited with an error"; exit 1; }
done

status=0
for frame in ${frames//,/ }; do
  name="$(printf 'frame_%05d.png' "$frame")"
  if [[ ! -f "$work/run1/$name" ]]; then
    echo "FAIL: frame $frame wasn't captured"; status=1
  elif cmp -s "$work/run1/$name" "$work/run2/$name"; then
    echo "frame $frame: identical"
  else
    echo "FAIL: frame $frame differs between two runs of the same replay"; status=1
  fi
done
exit $status
