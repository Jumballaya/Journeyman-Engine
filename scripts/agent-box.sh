#!/usr/bin/env bash
# A bare Linux container with only this tree's CLI pack (jm + the engine) on
# PATH: what an AI agent gets from a release, for bench/agent-runs/.
#
#   scripts/agent-box.sh <work folder>
#
# Builds jm and the engine for Linux inside Docker (from the committed tree,
# into build/agent-box), then starts the container "jmbox" with <work folder>
# at /work. It has curl and CA certificates, nothing else: no Node, no
# display, no GPU. Run commands with:
#   docker exec -w /work jmbox bash -lc '...'
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mkdir -p "$1" && cd "$1" && pwd)"
out="$root/build/agent-box"
src="$out/src"

rm -rf "$src" && mkdir -p "$src"
git -C "$root" archive HEAD | tar -x -C "$src"
docker run --rm -v "$src:/src" -v "$out:/out" ubuntu:24.04 bash -c '
  set -e
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build git ca-certificates curl golang-go \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev libgtk-3-dev >/dev/null
  cd /src
  cmake --preset release -B /out/cmake -DJM_BUILD_EDITOR=OFF -DJM_VERSION=agent-box >/dev/null
  cmake --build /out/cmake --target journeyman_engine | tail -1
  mkdir -p /out/pack
  (cd cli && GOTOOLCHAIN=auto go build -ldflags "-X main.version=agent-box" -o /out/pack/jm ./cmd/jm)
  cp /out/cmake/engine/journeyman_engine /out/pack/'

docker rm -f jmbox >/dev/null 2>&1 || true
docker run -d --name jmbox -v "$work:/work" -v "$out/pack:/opt/jm:ro" -w /work \
  -e PATH=/opt/jm:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin ubuntu:24.04 sleep infinity >/dev/null
docker exec jmbox bash -c 'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends curl ca-certificates >/dev/null 2>&1'
docker exec jmbox bash -c 'jm --version; command -v node >/dev/null && echo "node: present (should not be)" || echo "node: none"'
echo "jmbox is up; /work is $work"
