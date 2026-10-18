#!/usr/bin/env bash
# Records a play of a demo the way a person's is recorded (inputs through the
# window's event path: keys, clicks, a marker), then checks
# what the agent gets from it: the play replays the same to its end
# (jm plays verify), the state at the marker equals the state saved when it
# was made, a run held for F8's moment (frozen, picked, saved) ends where the
# same run with a plain marker does, and an image of the marker's moment comes out, drawn anew after a
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

# Enter through the menus, play with taps, holds and clicks, and hold a moment
# halfway (freeze; pick entities or points, an area; a note on one and the moment's; save); the plain run marks it instead.
python3 - "$work/commands.txt" "$work/commands-plain.txt" <<'PY'
import random, sys
random.seed(7)
out = ["step 40", "press Enter", "step 30"]
for i in range(30):
    out.append(f"step {random.randint(2, 9)}")
    r = random.random()
    if r < 0.3: out.append(random.choice(["press Enter", "press Space", "press ArrowLeft", "press ArrowRight", "press Z"]))
    elif r < 0.45: out.append(random.choice(["down ArrowLeft", "up ArrowLeft", "down Space", "up Space"]))
    elif r < 0.6: out.append(f"click {random.randint(10, 300)} {random.randint(10, 200)}")
    if i == 15: out.append("MOMENT")
out += ["up ArrowLeft", "up Space", "step 20", "state entities session", "quit"]
held = "\n".join(out).replace("MOMENT", "freeze\npick 120 100\npick 60 60 add\narea 20 30 140 120 add\npick 4 4 add\nnote 2 this one clips\nnote the check's marker\nsave")
open(sys.argv[1], "w").write(held + "\n")
open(sys.argv[2], "w").write("\n".join(out).replace("MOMENT", "marker the check's marker") + "\n")
PY

for run in held plain; do
  commands="$work/commands.txt" record="$play" out="$work/driven.txt"
  [ "$run" = plain ] && commands="$work/commands-plain.txt" record="$work/plain-play" out="$work/plain.txt"
  (cd "$demo/build" && JM_DRIVE=1 JM_HEADLESS=1 JM_SEED=7 JM_SAVE_DIR="$work/save-$run" JM_RECORD_DIR="$record" "$engine" . \
    <"$commands" >"$out" 2>"$work/$run-log.txt") || { cat "$work/$run-log.txt"; echo "FAIL: recording ($run) failed"; exit 1; }
  grep -q '"ok":false' "$out" && { grep '"ok":false' "$out"; echo "FAIL: a command failed ($run)"; exit 1; }
done

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
def last_state(path):
    s = [json.loads(l)["state"] for l in open(path) if '"state"' in l][-1]
    return s["entities"], s["session"]
show = json.load(open(f"{work}/show.json"))
verify = json.load(open(f"{work}/verify.json"))
state = json.load(open(f"{work}/state.json"))
frame = json.load(open(f"{work}/frame.json"))
marked = json.load(open(f"{play}/markers/1.json"))
fail = []
if len(show["markers"]) != 1 or show["markers"][0]["note"] != "the check's marker":
    fail.append(f"show: markers {show['markers']}")
elif not any(p["kind"] == "area" and "tiles" in p and "entities" in p for p in show["markers"][0].get("picks", [])):
    fail.append(f"show: the moment has no area with what's in it: {show['markers'][0].get('picks')}")
elif [p.get("note") for p in show["markers"][0]["picks"]][1:2] != ["this one clips"]:
    fail.append(f"show: pick 2's note isn't kept: {show['markers'][0]['picks']}")
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
if last_state(f"{work}/driven.txt") != last_state(f"{work}/plain.txt"):
    fail.append("a run held for a moment ends somewhere else than the same run with a plain marker")
if fail:
    print("FAIL:\n  " + "\n  ".join(fail)); sys.exit(1)
print(f"OK: {show['frames']} frames, marker at {m['frame']}: replays the same, a held moment changes nothing, state and image at the marker match, redrawn after a look change")
PY
