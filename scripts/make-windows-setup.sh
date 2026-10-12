#!/usr/bin/env bash
# Makes the one-file Windows install (scripts/windows/journeyman.iss) with Inno
# Setup's ISCC, from a folder holding journeyman_editor.exe, jm.exe,
# journeyman_engine.exe, journeyman_server.exe and LICENSE. Git Bash on Windows.
#   scripts/make-windows-setup.sh <folder> <out-setup.exe> [version]
# Signed with JM_WINDOWS_SIGNTOOL (an ISCC sign command using $f) if set.
set -euo pipefail
cd "$(dirname "$0")/.."

source="$1" out="$2" version="${3:-0.0.0}"
version="${version#v}"

iscc="$(command -v iscc || true)"
[[ -n "$iscc" ]] || iscc="$(cygpath -u "${PROGRAMFILES:-C:\\Program Files} (x86)")/Inno Setup 6/ISCC.exe"
[[ -x "$iscc" ]] || { echo "no Inno Setup 6 (ISCC.exe): choco install innosetup" >&2; exit 1; }

mkdir -p "$(dirname "$out")"
name="$(basename "$out" .exe)"
args=("/DVersion=$version" "/DNumericVersion=${version%%-*}" "/DSource=$(cygpath -w "$source")"
  "/O$(cygpath -w "$(dirname "$out")")" "/F$name")
[[ -n "${JM_WINDOWS_SIGNTOOL:-}" ]] && args+=("/Sjm=$JM_WINDOWS_SIGNTOOL" /DSigned)
# Git Bash would rewrite the /-options as paths.
MSYS2_ARG_CONV_EXCL='*' "$iscc" /Q "${args[@]}" "$(cygpath -w scripts/windows/journeyman.iss)"
