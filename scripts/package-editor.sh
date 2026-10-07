#!/usr/bin/env bash
# Builds the editor, the engine and jm, and puts them together:
#   macOS: dist/Journeyman Editor.app   (double-click; jm and the engine inside)
#   Linux: dist/journeyman-editor/      (run ./journeyman_editor)
# Building game scripts still needs Node.js on the machine.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release
cmake --build --preset release --target journeyman_engine journeyman_editor
(cd cli && go build -o ../build/bin/jm ./cmd/jm)

mkdir -p dist
if [[ "$(uname)" == "Darwin" ]]; then
  app="dist/Journeyman Editor.app"
  scripts/make-mac-app.sh build/release/editor/journeyman_editor build/release/engine/journeyman_engine build/bin/jm "$app"
  echo "Packaged $app"
else
  out="dist/journeyman-editor"
  rm -rf "$out"
  mkdir -p "$out"
  cp build/release/editor/journeyman_editor build/release/engine/journeyman_engine build/bin/jm "$out/"
  echo "Packaged $out"
fi
