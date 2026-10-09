#!/usr/bin/env bash
# Uses a release's Linux CLI the way a newcomer (or an agent) would, on a bare
# machine: unpack it, put it on PATH, make a game, build it, test it, export it.
# Fails at the first step that doesn't work.
#   scripts/verify-release.sh <folder with the release files> <version>
# Needs bash, tar and Node.js 20+ on PATH (the one thing the release doesn't bring).
set -euo pipefail

files="$(cd "$1" && pwd)" version="$2"
platform=linux-amd64
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
step() { echo; echo "== $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

step "unpack journeyman-cli-$platform.tar.gz and put it on PATH"
tar -xzf "$files/journeyman-cli-$platform.tar.gz" -C "$work"
export PATH="$work/journeyman-cli-$platform:$PATH"
got="$(jm --version)"
[[ "$got" == "jm version $version" ]] || fail "jm --version says '$got', expected $version"
echo "$got"

step "jm init"
mkdir "$work/game" && cd "$work/game"
jm init

step "jm generate script player"
jm generate script player
[[ -f assets/scripts/player.ts ]] || fail "no assets/scripts/player.ts"

step "jm build"
jm build
[[ -f build/.jm.json ]] || fail "no build/.jm.json"

step "jm test (passing)"
mkdir -p tests
cat > tests/rules.spec.ts <<'SPEC'
export function scoresAdd(): void {
  const score: i32 = 300;
  assert(score + 500 == 800, "300 + 500 is 800");
}
SPEC
jm test

step "jm test (a failing test fails the run)"
cat >> tests/rules.spec.ts <<'SPEC'
export function wrong(): void {
  const score: i32 = 300;
  assert(score == 301, "deliberately wrong");
}
SPEC
if jm test >"$work/failing.txt" 2>&1; then fail "jm test passed with a failing test"; fi
grep -q "deliberately wrong" "$work/failing.txt" || fail "the failure's message isn't in the output"
echo "failed, as it should"
rm tests/rules.spec.ts

step "jm export"
jm export --skip-build --out dist
game="dist/$(basename "$PWD")"
[[ -x "$game" ]] || fail "no exported game at $game"
engine="$work/journeyman-cli-$platform/journeyman_engine"
(( $(stat -c %s "$game") > $(stat -c %s "$engine") )) || fail "$game isn't bigger than the bare engine: no game inside"
ls -l "$game"

step "jm export --server"
jm export --skip-build --server --out dist
server="dist/$(basename "$PWD")-server"
[[ -x "$server" ]] || fail "no exported server at $server"
# No display, no GL: it hosts, runs its frames and stops.
JM_EXIT_AFTER_FRAMES=30 JM_NET_PORT=7799 "$server" || fail "$server didn't run"
ls -l "$server"

echo
echo "OK: $version's Linux CLI installs, builds, tests and exports a game and its server on a bare machine"
