#!/usr/bin/env bash
# Builds everything needed and launches Strike Wing 1942.
#   ./scripts/play-demo.sh            build + run in a window
#   ./scripts/play-demo.sh --export   build + produce a standalone app in demos/strike_wing/dist/
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' is required ($2)" >&2; exit 1; }
}
need cmake "https://cmake.org"
need ninja "https://ninja-build.org"
need go    "https://go.dev, 1.24+"
need node  "https://nodejs.org, 20+"
need npm   "ships with Node.js"

echo "==> Building engine (release)"
./scripts/build-release.sh >/dev/null

echo "==> Building jm CLI"
mkdir -p build/bin
(cd cli && go build -o "$root/build/bin/jm" ./cmd/jm)
jm="$root/build/bin/jm"

echo "==> Installing script toolchain (AssemblyScript)"
if [ ! -d demos/${1}/assets/scripts/node_modules/assemblyscript ]; then
  (cd demos/${1}/assets/scripts && npm install --no-audit --no-fund)
fi

cd demos/${1}
echo "==> Building game"
"$jm" build >/dev/null

if [ "${1:-}" = "--export" ]; then
  "$jm" export --skip-build
else
  echo "==> Launching"
  "$jm" run
fi
