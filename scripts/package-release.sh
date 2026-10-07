#!/usr/bin/env bash
# Builds one platform's release files into dist/release/:
#   journeyman-player-<version>-<target>[.exe]      the engine alone, for `jm export --target`
#   journeyman-agent-<version>-<target>.tar.gz|zip   jm + the engine
#   journeyman-human-<version>-<target>.zip|tar.gz   the editor, with jm and the engine
#
#   scripts/package-release.sh v0.0.1 darwin-arm64
# Targets: darwin-arm64, darwin-amd64, linux-amd64, windows-amd64. Run on that
# OS (darwin-amd64 cross-compiles on an Apple Silicon Mac too).
set -euo pipefail
cd "$(dirname "$0")/.."

version="$1" target="$2"
os="${target%-*}" arch="${target#*-}"
exe=""
[[ "$os" == windows ]] && exe=".exe"

build="build/release-$target"
cmake_args=(--preset release -B "$build")
[[ "$target" == darwin-amd64 ]] && cmake_args+=(-DCMAKE_OSX_ARCHITECTURES=x86_64)
cmake "${cmake_args[@]}"
cmake --build "$build" --target journeyman_engine journeyman_editor

mkdir -p "$build/bin"
(cd cli && CGO_ENABLED=0 GOOS="$os" GOARCH="$arch" go build -trimpath \
  -ldflags "-s -w -X main.version=$version" -o "../$build/bin/jm$exe" ./cmd/jm)

engine="$build/engine/journeyman_engine$exe"
editor="$build/editor/journeyman_editor$exe"
jm="$build/bin/jm$exe"
out="dist/release"
mkdir -p "$out"

# archive <name> <folder>: zip on Windows, tar.gz elsewhere; the folder is the archive's top level.
archive() {
  local name="$1" dir="$2"
  if [[ "$os" == windows ]]; then
    (cd "$(dirname "$dir")" && 7z a -tzip -bso0 "$OLDPWD/$out/$name.zip" "$(basename "$dir")")
  else
    tar -C "$(dirname "$dir")" -czf "$out/$name.tar.gz" "$(basename "$dir")"
  fi
}

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

cp "$engine" "$out/journeyman-player-$version-$target$exe"

agent="$staging/journeyman-$version-$target"
mkdir -p "$agent"
cp "$jm" "$engine" LICENSE "$agent/"
archive "journeyman-agent-$version-$target" "$agent"

if [[ "$os" == darwin ]]; then
  # A zip made by ditto keeps the bundle's signature and symlinks intact.
  human="$staging/Journeyman Editor.app"
  scripts/make-mac-app.sh "$editor" "$engine" "$jm" "$human" "$version"
  ditto -c -k --norsrc --noextattr --keepParent "$human" "$out/journeyman-human-$version-$target.zip"
else
  human="$staging/journeyman-editor-$version-$target"
  mkdir -p "$human"
  cp "$editor" "$jm" "$engine" LICENSE "$human/"
  archive "journeyman-human-$version-$target" "$human"
fi

echo "Packaged $target:"
ls -1 "$out" | grep -- "-$target" | sed 's/^/  /'
