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

echo "==> Building the engine and jm (build/bin)"
./scripts/build-release.sh >/dev/null
jm="$root/build/bin/jm"

cd demos/${1}
echo "==> Building game"
"$jm" build >/dev/null

if [ "${1:-}" = "--export" ]; then
  "$jm" export --skip-build
else
  echo "==> Launching"
  "$jm" run
fi
