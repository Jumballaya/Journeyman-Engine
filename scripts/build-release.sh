#!/usr/bin/env bash
# Builds the engine, the dedicated server and the editor (release), and jm,
# and puts jm, the engine and the server together in build/bin as a release
# does: build/bin/jm finds the programs beside it, so games name no engine path.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

cmake --preset release
cmake --build --preset release
mkdir -p build/bin
(cd cli && go build -o "$root/build/bin/jm" ./cmd/jm)
exe=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) exe=".exe" ;; esac
for program in journeyman_engine journeyman_server; do
  cp "build/release/engine/$program$exe" "build/bin/$program$exe"
done
echo "build/bin: jm, journeyman_engine, journeyman_server (put it on PATH, or run build/bin/jm)"
