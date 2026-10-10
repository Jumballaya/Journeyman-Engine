#!/usr/bin/env bash
# Records a play of a demo the way a person's is recorded (inputs through the
# window's event path: keys, clicks, uneven frame times, a marker), then checks
# what the agent gets from it: the play replays the same to its end
# (jm plays verify), the state at the marker equals the state saved when it
# was made, and an image of the marker's moment comes out, drawn anew after a
# change to only how the game looks.
#
#   scripts/check-plays.sh <demo folder> [jm]
# The demo must be built; the engine is found as jm finds it ($JM_ENGINE, or
# beside jm). An image needs GL: run under xvfb-run on Linux.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
demo="$(cd "$1" && pwd)"
jm="${2:-$root/build/bin/jm}"
engine="${JM_ENGINE:-$(dirname "$jm")/journeyman_engine}"
id="ci_$(date +%s)_$$"
play="$demo/.jm/plays/$id"
work="$(mktemp -d)"
look="$demo/build/check-plays-look.txt"
trap 'rm -rf "$work" "$play" "$play-altered" "$look"' EXIT

# Enter through the menus, play with taps, holds and clicks at uneven frame
# times, and mark a moment halfway.
python3 - "$work/commands.txt" <<'PY'
import random, sys
random.seed(7)
out = ["step 40", "press Enter", "step 30"]
for i in range(30):
    out.append(f"step {random.randint(2, 9)} {random.choice(['0.0167', '0.0133', '0.0211', '0.0333'])}")
    r = random.random()
    if r < 0.3: out.append(random.choice(["press Enter", "press Space", "press ArrowLeft", "press ArrowRight", "press Z"]))
    elif r < 0.45: out.append(random.choice(["down ArrowLeft", "up ArrowLeft", "down Space", "up Space"]))
    elif r < 0.6: out.append(f"click {random.randint(10, 300)} {random.randint(10, 200)}")
    if i == 15: out.append("marker the check's marker")
out += ["up ArrowLeft", "up Space", "step 20", "quit"]
open(sys.argv[1], "w").write("\n".join(out) + "\n")
PY

(cd "$demo/build" && JM_DRIVE=1 JM_HEADLESS=1 JM_SAVE_DIR="$work/save" JM_RECORD_DIR="$play" "$engine" . \
  <"$work/commands.txt" >"$work/driven.txt" 2>"$work/driven-log.txt") || { cat "$work/driven-log.txt"; echo "FAIL: recording failed"; exit 1; }
grep -q '"ok":false' "$work/driven.txt" && { grep '"ok":false' "$work/driven.txt"; echo "FAIL: a command failed"; exit 1; }

cd "$demo"
"$jm" plays show "$id" --json >"$work/show.json"
"$jm" plays verify "$id" --json >"$work/verify.json"
"$jm" plays state "$id" m1 entities >"$work/state.json"
"$jm" plays frame "$id" m1 --json >"$work/frame.json"
# A file only the look depends on: the play still replays the same, but its
# frames are drawn by the changed build, and say so.
echo "a change to how the game looks" >"$look"
"$jm" plays frame "$id" m1 --json >"$work/frame-look.json"
"$jm" plays verify "$id" --json >"$work/verify-look.json"
rm "$look"
# The same play without the player's keys after the first second can't go the
# same way: verify must say so.
cp -R "$play" "$play-altered"
python3 - "$play-altered/inputs.jsonl" <<'PY'
import json, sys
lines = [l for l in open(sys.argv[1]) if not (json.loads(l).get("type") == "key" and json.loads(l)["f"] > 60)]
open(sys.argv[1], "w").write("".join(lines))
PY
"$jm" plays verify "$id-altered" --json >"$work/verify-altered.json"

python3 - "$work" "$play" <<'PY'
import json, os, sys
work, play = sys.argv[1], sys.argv[2]
show = json.load(open(f"{work}/show.json"))
verify = json.load(open(f"{work}/verify.json"))
state = json.load(open(f"{work}/state.json"))
frame = json.load(open(f"{work}/frame.json"))
marked = json.load(open(f"{play}/markers/1.json"))
fail = []
if len(show["markers"]) != 1 or show["markers"][0]["note"] != "the check's marker":
    fail.append(f"show: markers {show['markers']}")
if not verify["same"]:
    fail.append(f"verify: the play replays differently from frame {verify.get('differsBy')}")
altered = json.load(open(f"{work}/verify-altered.json"))
if altered["same"]:
    fail.append("verify: a play with its inputs taken out still replays 'the same'")
if verify["errors"]:
    fail.append(f"verify: errors on the way: {verify['errors'][:3]}")
m = show["markers"][0]
if state["frame"] != m["frame"] or state["state"]["entities"] != marked["entities"]:
    fail.append(f"state at the marker (frame {state['frame']}) isn't what was saved when it was made (frame {m['frame']})")
if not os.path.getsize(frame["path"]) or (frame["source"]["kind"], frame["source"].get("drift")) != ("replay", "same"):
    fail.append(f"frame: {frame}")
look = json.load(open(f"{work}/frame-look.json"))
if look["path"] == frame["path"] or (look["source"]["kind"], look["source"].get("drift")) != ("replay", "look"):
    fail.append(f"frame after a look-only change: the earlier image, or not said: {look}")
if not json.load(open(f"{work}/verify-look.json"))["same"]:
    fail.append("verify: a look-only change made the play replay differently")
if fail:
    print("FAIL:\n  " + "\n  ".join(fail)); sys.exit(1)
print(f"OK: {show['frames']} frames, marker at {m['frame']}: replays the same, state and image at the marker match, redrawn after a look change")
PY
