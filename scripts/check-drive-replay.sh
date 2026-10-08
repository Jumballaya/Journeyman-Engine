#!/usr/bin/env bash
# Drives a built game (JM_DRIVE, no GL) through a fixed run of key commands
# while recording it (JM_DRIVE_RECORD), replays the recording, and fails
# unless the replay reaches the same state: a bug found by driving is
# reproduced by its recording, frame for frame.
#
#   scripts/check-drive-replay.sh <build folder> [engine]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
game="$(cd "$1" && pwd)"
engine="${2:-$root/build/release/engine/journeyman_engine}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Enter through the menus, then short taps (a frame early or late shows) and
# longer holds; 265 frames in all.
commands='step 40
press Enter
step 60
down ArrowLeft
down Space
step 7
up ArrowLeft
step 5
down ArrowUp
step 9
up ArrowUp
press Enter
step 30
down ArrowRight
step 11
up ArrowRight
press Z
step 3
press ArrowDown
step 6
down ArrowLeft
step 50
up ArrowLeft
up Space
step 44
state
quit'

(cd "$game" && env -u DISPLAY -u WAYLAND_DISPLAY JM_DRIVE=1 JM_RENDERER=none JM_STRICT=1 \
  JM_SAVE_DIR="$work/save-driven" JM_DRIVE_RECORD="$work/recording.txt" "$engine" . \
  <<<"$commands" >"$work/driven.txt" 2>"$work/driven-log.txt") || { cat "$work/driven-log.txt"; echo "FAIL: the driven run failed"; exit 1; }
grep -q '"ok":false' "$work/driven.txt" && { cat "$work/driven.txt"; echo "FAIL: a driver command failed"; exit 1; }
grep '"state"' "$work/driven.txt" >"$work/driven-state.json" || { cat "$work/driven.txt"; echo "FAIL: no state answer"; exit 1; }
frame="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["state"]["frame"])' "$work/driven-state.json")"

(cd "$game" && env -u DISPLAY -u WAYLAND_DISPLAY JM_RENDERER=none JM_STRICT=1 JM_INPUT_REPLAY="$work/recording.txt" \
  JM_SAVE_DIR="$work/save-replay" JM_EXIT_AFTER_FRAMES=$((frame + 2)) JM_DUMP_DIR="$work/replay" JM_DUMP_FRAMES="$frame" \
  "$engine" . >"$work/replay-log.txt" 2>&1) || { cat "$work/replay-log.txt"; echo "FAIL: the replay failed"; exit 1; }

python3 - "$work" "$frame" <<'PY'
import json, sys
work, frame = sys.argv[1], int(sys.argv[2])
driven = json.load(open(f"{work}/driven-state.json"))["state"]
replay = json.load(open(f"{work}/replay/state_{frame:05d}.json"))
diff = [k for k in sorted(set(driven) | set(replay)) if driven.get(k) != replay.get(k)]
if diff:
    print(f"FAIL: at frame {frame} the replay differs from the driven run in: {', '.join(diff)}")
    print(open(f"{work}/recording.txt").read())
    sys.exit(1)
print(f"OK: the recording replays to the driven state at frame {frame}")
PY
