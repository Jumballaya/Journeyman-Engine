#!/usr/bin/env bash
# Runs a built game through an .app's own jm (and so its engine), headless,
# writing into a relative folder: the output must land in the game's folder.
# GLFW in an .app may move the engine's cwd to Contents/Resources. Without GL
# (a CI Mac) it runs with JM_RENDERER=none, which never starts GLFW.
#
#   scripts/check-mac-bundle-run.sh <app> <game folder (built)>
set -euo pipefail
app="$(cd "$1" && pwd)"
cd "$2"
rm -rf frames
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
if JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=12 JM_CAPTURE_DIR=frames JM_CAPTURE_FRAMES=10 "$app/Contents/MacOS/jm" run 2>&1 | tee "$log"; then
  [[ -f frames/frame_00010.png ]] || fail "the bundled engine's capture isn't in $PWD/frames/"
  echo "the bundled engine's capture is in frames/"
elif grep -q "GLFW window creation failed" "$log"; then
  echo "no GL here: running without it, which can't show GLFW's cwd change (check that on a Mac with a GPU)"
  JM_RENDERER=none JM_EXIT_AFTER_FRAMES=12 JM_DUMP_DIR=frames JM_DUMP_FRAMES=10 "$app/Contents/MacOS/jm" run
  [[ -f frames/state_00010.json ]] || fail "the bundled engine's state dump isn't in $PWD/frames/"
  echo "the bundled engine's state dump is in frames/"
else
  fail "the bundled jm couldn't run the game"
fi
