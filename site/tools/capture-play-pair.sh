#!/bin/bash
# The play shot runs on real time, so two separate runs drift. This captures it
# once: play the Aldane battle, pause at frame 1000, save the dark frame, switch
# the editor to light, save the light frame. Same paused game in both.
# Writes $JM_CAPTURE_WORK/out/ed2/play-{dark,light}.png. Run capture-editor.sh first
# (it refreshes and pre-builds the scratch projects there).
set -e
REPO="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
C="${JM_CAPTURE_WORK:-${TMPDIR:-/tmp}/jm-site-capture}"
SET="$HOME/Library/Application Support/Journeyman Editor"
BK="$C/settings-bk-pair"
O="$C/out/ed2"
rm -rf "$BK"; cp -Rp "$SET" "$BK"
trap 'rm -rf "$SET"; cp -Rp "$BK" "$SET"; diff -r "$BK" "$SET" >/dev/null && echo "editor settings restored"' EXIT
PLAY="100:play.toggle;300:@mouse 760 400"
for f in $(seq 400 60 940); do PLAY="$PLAY;$f:@press Z"; done
PLAY="$PLAY;1000:play.pause;1010:@shot $O/play-dark.png;1030:view.appearance.light;1060:@shot $O/play-light.png"
(cd "$REPO" && JM_HEADLESS=1 JM_EDITOR_APPEARANCE=dark JM_EDITOR_SIZE=1600x1000 \
  JM_EDITOR_PROJECT="$C/ed/jrpg" JM_EDITOR_SCENE=scenes/battle.scene.json \
  JM_EDITOR_SCRIPT="$PLAY" JM_EDITOR_CAPTURE="$O/play-end.png" JM_EDITOR_FRAMES=1080 \
  ./build/release/editor/journeyman_editor > "$O/play-pair.log" 2>&1)
echo "play-dark.png and play-light.png written"
