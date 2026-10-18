#!/usr/bin/env bash
# Checks Windows programs are GUI programs, so no console window opens with
# them, and that the engine and the player still answer through a pipe (how jm talks to it).
# Git Bash on Windows (CI).
#   scripts/check-windows-gui.sh <program.exe>...
set -euo pipefail

fail() { echo "FAIL: $*" >&2; exit 1; }
# The PE header's subsystem: 2 is a GUI program, 3 a console one.
subsystem() { od -An -tu2 -j $(($(od -An -tu4 -j 60 -N 4 "$1") + 92)) -N 2 "$1" | tr -d ' '; }

for exe in "$@"; do
  [[ "$(subsystem "$exe")" == 2 ]] || fail "$exe would open a console window"
  echo "$exe: a GUI program"
  name="$(basename "$exe" .exe)"
  if [[ "$name" == journeyman_engine || "$name" == journeyman_player ]]; then
    out="$("$exe" --version | tr -d '\r')"
    [[ "$out" == "$name "* ]] || fail "$exe --version didn't come through a pipe (got '$out')"
    echo "$exe: $out, through a pipe"
  fi
done
