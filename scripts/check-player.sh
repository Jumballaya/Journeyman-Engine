#!/usr/bin/env bash
# The player (journeyman_player, what `jm export` ships) holds none of the
# agent tools (engine/core/app/AgentTools.hpp: the driver, live control,
# plays), and a game exported with it starts: it's still running after a few
# seconds, and its log says the engine started and loaded a scene, with no error.
#
#   scripts/check-player.sh <demo folder> [jm] [engine folder]
# The engine folder holds journeyman_engine and journeyman_player (default
# build/release/engine). Linux: the player reads no JM_* switch, so it opens
# a window and runs until stopped: run under xvfb-run, and `timeout` stops it.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
demo="$(cd "$1" && pwd)"
jm="${2:-$root/build/bin/jm}"
engines="${3:-$root/build/release/engine}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Strings only the agent tools hold: a class's name, the driver's, a play's file, live control's,
# F8's moment (its module, driver commands, what an area holds, a note's placeholder), the
# modules' tools (<module>/agent: their modules, input replays, captures, the driver's
# commands for them) and hot reload's switch.
agent="AgentRun|unknown command '|timeline\.jsonl|JM_CONTROL|MomentsModule|not frozen|moreEntities|type a note"
agent+="|InputsAgentModule|Renderer2DAgentModule|Physics2DAgentModule|replaying \{\} input events|JM_DRIVE_RECORD"
agent+="|frame_%05llu|capture takes a path|debug takes physics|near takes tag=|JM_WATCH"
# Each must be in journeyman_engine, so a stale one can't pass silently.
strings -a "$engines/journeyman_engine" >"$work/engine-strings.txt"
IFS='|' read -ra patterns <<<"$agent"
for pattern in "${patterns[@]}"; do
  grep -Eq "$pattern" "$work/engine-strings.txt" || { echo "FAIL: '$pattern' isn't in journeyman_engine either: the check is stale"; exit 1; }
done
if found="$(strings -a "$engines/journeyman_player" | grep -E "$agent")"; [ -n "$found" ]; then
  printf 'FAIL: journeyman_player holds agent tools:\n%s\n' "$found"; exit 1
fi
echo "journeyman_player holds no agent tools"

(cd "$demo" && JM_PLAYER="$engines/journeyman_player" "$jm" export --out "$work/dist" >/dev/null)
game="$(find "$work/dist" -maxdepth 1 -type f -perm -u+x | head -1)"
[ -n "$game" ] || { echo "FAIL: jm export wrote no game"; exit 1; }

# Run from an empty folder: only the game carried inside can start. A bundled
# game logs into its data folder ($XDG_DATA_HOME/<name>/logs on Linux).
mkdir "$work/elsewhere" "$work/data"
status=0
(cd "$work/elsewhere" && XDG_DATA_HOME="$work/data" timeout -k 5 8 "$game" </dev/null >"$work/out.txt" 2>&1) || status=$?
log="$(find "$work/data" -name engine.log | head -1)"
show() {
  cat "$work/out.txt"
  if [ -n "$log" ]; then cat "$log"; fi
}
[ "$status" = 124 ] || { show; echo "FAIL: $(basename "$game") ended by itself (exit $status) before it was stopped"; exit 1; }
[ -n "$log" ] || { show; echo "FAIL: $(basename "$game") wrote no log"; exit 1; }
grep -q "Journeyman Engine Starting up" "$log" && grep -q "\[SceneManager\] loaded '" "$log" \
  || { show; echo "FAIL: $(basename "$game") didn't start its game"; exit 1; }
if grep -E "\] \[(error|critical)\] " "$log"; then echo "FAIL: $(basename "$game") logged errors"; exit 1; fi
echo "OK: $(basename "$game") (journeyman_player) started its game and ran until stopped"
