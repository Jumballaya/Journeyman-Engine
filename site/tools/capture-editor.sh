#!/bin/bash
# site/tools/capture-editor.sh <dark|light>
#
# Recreates the five editor screenshots (scene, tiles, data, ui, play) as
# out/ed2/<name>-<suffix>.png at 3200x2000 (JM_EDITOR_SIZE=1600x1000 points on a
# 2x display). The steps per shot are exactly the ones used for the site
# captures, so two runs that differ only in apply_theme frame identically.
#
# It never runs against the repo's demos: each run refreshes scratch copies in
# ed/ from the repo, pre-builds them with jm, and backs up / restores the
# editor's settings directory around the editor runs.
set -euo pipefail

if [ $# -ne 1 ] || [ -z "$1" ]; then
  echo "usage: $0 <suffix>" >&2
  exit 2
fi
SUFFIX="$1"

REPO="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
# Scratch space for project copies and raw captures (never the repo's demos).
W="${JM_CAPTURE_WORK:-${TMPDIR:-/tmp}/jm-site-capture}"
JM="$REPO/build/bin/jm"
EDITOR="$REPO/build/release/editor/journeyman_editor"
SETTINGS="$HOME/Library/Application Support/Journeyman Editor"
ED="$W/ed"                 # scratch project copies
OUTDIR="$W/out/ed2"
SIZE=1600x1000

# ---------------------------------------------------------------------------
# THEME HOOK
# Empty for now. Once the editor's dark/light appearance setting lands
# (editor/Theme.cpp), set it here: either export an env var the editor reads,
# or write the appearance into the settings file under "$SETTINGS" (this runs
# AFTER the backup, so whatever it writes is undone by the restore).
# Called once, before any editor run, with the suffix given on the command line.
apply_theme() {
  # JM_EDITOR_APPEARANCE pins the editor's palette (dark|light) for automation.
  export JM_EDITOR_APPEARANCE="$1"
}
# ---------------------------------------------------------------------------

# --- settings backup / restore ---------------------------------------------
BACKUP="$W/editor-settings-backup-$SUFFIX-$$"
HAD_SETTINGS=0
restore_settings() {
  if [ "$HAD_SETTINGS" = 1 ]; then
    rm -rf "$SETTINGS"
    cp -Rp "$BACKUP" "$SETTINGS"
    if diff -r "$SETTINGS" "$BACKUP" >/dev/null; then
      echo "editor settings restored (identical to backup)"
      rm -rf "$BACKUP"
    else
      echo "WARNING: settings differ from backup; backup kept at $BACKUP" >&2
    fi
  else
    rm -rf "$SETTINGS"   # there was none before the runs
  fi
}
if [ -d "$SETTINGS" ]; then
  cp -Rp "$SETTINGS" "$BACKUP"
  HAD_SETTINGS=1
fi
trap restore_settings EXIT

# --- refresh scratch copies from the repo and pre-build them ----------------
# Same as the original captures: plain cp -R of each demo plus demos/common
# side by side (jm needs ../common), then `jm build` in each copy so the
# editor opens to "Up to date" instead of the "Building the project" screen.
DEMOS=(jrpg dungeon "Ash and Iron" strike_wing)
mkdir -p "$ED" "$OUTDIR"
rm -rf "$ED/common"
cp -R "$REPO/demos/common" "$ED/"
for d in "${DEMOS[@]}"; do
  rm -rf "$ED/$d"
  cp -R "$REPO/demos/$d" "$ED/"
  (cd "$ED/$d" && "$JM" build > /dev/null)
done

apply_theme "$SUFFIX"

# --- one editor capture -----------------------------------------------------
# shot <name> <project dir> <scene> <frames> <script>
shot() {
  local name="$1" project="$2" scene="$3" frames="$4" script="$5"
  local out="$OUTDIR/$name-$SUFFIX.png"
  rm -f "$out"
  (cd "$REPO" && JM_HEADLESS=1 JM_EDITOR_SIZE="$SIZE" \
     JM_EDITOR_PROJECT="$project" JM_EDITOR_SCENE="$scene" \
     JM_EDITOR_SCRIPT="$script" JM_EDITOR_CAPTURE="$out" JM_EDITOR_FRAMES="$frames" \
     "$EDITOR" > "$out.log" 2>&1)
  if [ -f "$out" ]; then
    echo "$name: $(python3 -c "from PIL import Image;print('%dx%d' % Image.open('$out').size)") -> $out"
  else
    echo "$name: FAILED (see $out.log)" >&2
  fi
}

# Coordinates are in window points (1600x1000). "@mouse 1000 992" parks the
# mouse on the status bar so no hover tooltip lands in the shot.

# scene: Embers of Aldane town, Scene tab, Elder selected, map panned to centre.
shot scene "$ED/jrpg" scenes/town.scene.json 600 \
  "100:@click 318 84;200:@select Elder;250:@mdrag 869 312 762 398;400:@mouse 1000 992"

# tiles: Hollow Grove map in paint mode (B), water tile picked from the
# palette, two strokes painted, brush left hovering over the canvas.
shot tiles "$ED/dungeon" scenes/grove.scene.json 400 \
  "100:@click 318 84;150:@mdrag 837 333 762 398;200:@select map;210:@key B;230:@click 558 603;250:@mouse 600 290;252:@down;256:@mouse 630 290;260:@mouse 660 292;264:@mouse 690 292;268:@mouse 720 295;272:@up;276:@mouse 600 310;278:@down;282:@mouse 640 312;286:@mouse 700 314;290:@up;300:@mouse 735 330"

# data: Ash and Iron items table, row 6 (Old Revolver) selected via its row number.
shot data "$ED/Ash and Iron" scenes/cinderwell.scene.json 500 \
  "200:@open assets/data/items.json;300:@click 300 386;400:@mouse 1000 992"

# ui: Strike Wing title.ui.html in the UI editor, "HOW TO PLAY" item selected.
shot ui "$ED/strike_wing" scenes/title.scene.json 500 \
  "200:@open assets/ui/title.ui.html;300:@click 877 452;400:@mouse 1000 992"

# play: Embers of Aldane battle in the Game panel, Z pressed every 60 frames
# from 400 to 1400, captured at frame 1000 (the FIGHT menu shot).
PLAY="100:play.toggle;300:@mouse 760 400"
for f in $(seq 400 60 1400); do PLAY="$PLAY;$f:@press Z"; done
shot play "$ED/jrpg" scenes/battle.scene.json 1000 "$PLAY"
