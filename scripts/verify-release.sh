#!/usr/bin/env bash
# Uses a release's CLI the way a newcomer (or an agent) would: install it with
# install.sh, put it on PATH, make a game, build it, test it, export it.
# Fails at the first step that doesn't work.
#   scripts/verify-release.sh <folder with the release files> <version> [platform]
# platform: linux-amd64 (default; a bare machine: no Node.js, the first build
# downloads the toolchain), darwin-arm64 (a CI Mac, which has Node) or
# windows-amd64 (Git Bash on a CI Windows machine; installs with install.ps1).
set -euo pipefail

files="$(cd "$1" && pwd)" version="$2" platform="${3:-linux-amd64}"
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
step() { echo; echo "== $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
bare() { [[ "$platform" == linux-* ]]; }
exe=""
[[ "$platform" == windows-* ]] && exe=".exe"

step "install from the release files, then put it on PATH"
if [[ ! -f "$files/SHA256SUMS" ]]; then
  (cd "$files" && if command -v sha256sum >/dev/null; then sha256sum ./*; else shasum -a 256 ./*; fi | sed 's| \*\{0,1\}\./|  |' >SHA256SUMS)
fi
if [[ -n "$exe" ]]; then
  JM_FROM="$(cygpath -w "$files")" JM_VERSION="$version" JM_INSTALL_DIR="$(cygpath -w "$work/jm")" \
    powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$here/install.ps1")"
else
  JM_FROM="$files" JM_VERSION="$version" JM_INSTALL_DIR="$work/jm" sh "$here/install.sh"
fi
export PATH="$work/jm/bin:$PATH"
got="$(jm --version)"
[[ "$got" == "jm version $version" ]] || fail "jm --version says '$got', expected $version"
echo "$got"

if [[ "$platform" == darwin-* ]]; then
  # Built on the newest macOS, a binary needs that macOS unless told otherwise.
  step "every binary runs on the macOS the app asks for (LSMinimumSystemVersion)"
  unzip -q "$files/journeyman-editor-$platform.zip" -d "$work/app"
  app="$work/app/Journeyman Editor.app"
  min="$(plutil -extract LSMinimumSystemVersion raw "$app/Contents/Info.plist")"
  for bin in "$app/Contents/MacOS"/*; do
    minos="$(vtool -show-build "$bin" | awk '/minos/ {print $2; exit}')"
    [[ -n "$minos" ]] && printf '%s\n%s\n' "$minos" "$min" | sort -V -C || fail "$(basename "$bin") needs macOS $minos, the app says $min"
    echo "$(basename "$bin"): macOS $minos+"
  done
  step "the .pkg installs the editor and puts jm on PATH"
  pkg="$files/journeyman-$platform.pkg"
  sudo installer -pkg "$pkg" -target /
  [[ "$(/usr/local/bin/jm --version)" == "jm version $version" ]] || fail "the .pkg's jm isn't $version"
  [[ -x "/Applications/Journeyman Editor.app/Contents/MacOS/journeyman_editor" ]] || fail "the .pkg didn't install the editor"
  if [[ "${JM_EXPECT_NOTARIZED:-}" == true ]]; then
    pkgcheck="$(spctl --assess --type install -vv "$pkg" 2>&1)" || true
    echo "$pkgcheck"
    grep -q "source=Notarized Developer ID" <<<"$pkgcheck" || fail "Gatekeeper doesn't see a notarized .pkg"
    step "the editor and jm are Developer ID signed, and Gatekeeper opens the app"
    gatekeeper="$(spctl --assess --type execute -vv "$app" 2>&1)" || true
    echo "$gatekeeper"
    grep -q "source=Notarized Developer ID" <<<"$gatekeeper" || fail "Gatekeeper doesn't see a notarized app"
    grep -q "Authority=Developer ID Application" <<<"$(codesign -dvv "$(command -v jm)" 2>&1)" || fail "jm isn't Developer ID signed"
  fi
fi

step "jm doctor --json (engine matches, nothing stops a build)"
if bare && command -v node >/dev/null; then fail "this machine has node; it should be bare"; fi
doctor="$(jm doctor --json)" || { echo "$doctor"; fail "jm doctor found an error"; }
echo "$doctor"
grep -q "\"version\": \"$version\"" <<<"$doctor" || fail "doctor doesn't report $version"
grep -q '"schema": true' <<<"$doctor" || fail "the engine doesn't answer --schema"
grep -q "the engine is" <<<"$doctor" && fail "jm and the engine are different versions"

step "jm init"
mkdir "$work/game" && cd "$work/game"
jm init

step "jm generate script player"
jm generate script player
[[ -f assets/scripts/player.ts ]] || fail "no assets/scripts/player.ts"

step "attach the script to the main scene"
cat > scenes/main.scene.json <<'SCENE'
{"name": "main", "entities": [{"name": "Player", "components": {"ScriptComponent": {"script": "assets/scripts/player.ts"}}}]}
SCENE

step "jm build (downloads Node and AssemblyScript)"
jm build
[[ -f build/.jm.json ]] || fail "no build/.jm.json"
cmp -s -n 4 <(printf '\0asm') build/assets/scripts/player.ts || fail "build/assets/scripts/player.ts isn't WebAssembly"
if bare; then jm doctor --json | grep -q '"nodeSource": "managed"' || fail "the build didn't use the downloaded Node"; fi

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
name="$(basename "$PWD")"
game="dist/$name$exe"
if [[ "$platform" == darwin-* ]]; then
  game="dist/$name.app/Contents/MacOS/$name"
  codesign --verify "dist/$name.app" || fail "the exported app isn't signed: Apple silicon won't run it"
fi
[[ -x "$game" ]] || fail "no exported game at $game"
# Run from an empty folder: only a game carried inside can start (a bare engine has none).
game="$PWD/$game"
(mkdir -p "$work/elsewhere" && cd "$work/elsewhere" && JM_RENDERER=none JM_EXIT_AFTER_FRAMES=30 "$game") \
  || fail "$game doesn't run its game"
ls -l "$game"

step "jm export --server"
jm export --skip-build --server --out dist
server="dist/$(basename "$PWD")-server$exe"
[[ -x "$server" ]] || fail "no exported server at $server"
# No display, no GL: it hosts, runs its frames and stops.
JM_EXIT_AFTER_FRAMES=30 JM_NET_PORT=7799 "$server" || fail "$server didn't run"
ls -l "$server"

echo
echo "OK: $version's $platform CLI installs, builds, tests and exports a game and its server"
