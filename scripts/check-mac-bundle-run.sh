#!/usr/bin/env bash
# Runs a built game through an .app's own jm (and so its engine), headless
# with OpenGL, capturing into a relative folder: the frame must land in the
# game's folder. GLFW in an .app may move the engine's cwd to Contents/Resources.
#
#   scripts/check-mac-bundle-run.sh <app> <game folder (built)>
set -euo pipefail
app="$(cd "$1" && pwd)"
cd "$2"
rm -rf frames
JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=12 JM_CAPTURE_DIR=frames JM_CAPTURE_FRAMES=10 "$app/Contents/MacOS/jm" run
[[ -f frames/frame_00010.png ]] || { echo "FAIL: the bundled engine's capture isn't in $PWD/frames/" >&2; exit 1; }
echo "the bundled engine's capture is in frames/"
