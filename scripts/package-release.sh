#!/usr/bin/env bash
# Builds one platform's release files into dist/release/:
#   journeyman-cli-<target>.tar.gz|zip      jm + the engine and server: scripts, CI, AI agents
#   journeyman-editor-<target>.zip|tar.gz   the editor, with jm, the engine and the server inside
#   journeyman-engine-<target>[.exe]        the engine alone, for `jm export --target`
#   journeyman-server-<target>[.exe]        the dedicated server alone, for `jm export --server --target`
# File names carry no version, so the newest of each is always at
# https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/<name>;
# each archive unpacks to a folder of the same name; `jm --version` says which release.
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
cmake_args=(--preset release -B "$build" -DJM_VERSION="$version")
[[ "$target" == darwin-amd64 ]] && cmake_args+=(-DCMAKE_OSX_ARCHITECTURES=x86_64)
cmake "${cmake_args[@]}"
cmake --build "$build" --target journeyman_engine journeyman_server journeyman_editor

mkdir -p "$build/bin"
(cd cli && CGO_ENABLED=0 GOOS="$os" GOARCH="$arch" go build -trimpath \
  -ldflags "-s -w -X main.version=$version" -o "../$build/bin/jm$exe" ./cmd/jm)

engine="$build/engine/journeyman_engine$exe"
server="$build/engine/journeyman_server$exe"
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

# macOS: everything signed (Developer ID with JM_SIGN_IDENTITY, else ad-hoc) and,
# with a notary key, notarized before it's packed (see scripts/sign-mac.sh).
if [[ "$os" == darwin ]]; then
  scripts/sign-mac.sh sign "$engine" "$server" "$jm"
  app="$staging/Journeyman Editor.app"
  scripts/make-mac-app.sh "$editor" "$engine" "$jm" "$app" "$version" "$server"
  scripts/sign-mac.sh notarize "$engine" "$server" "$jm" "$app"
fi

cp "$engine" "$out/journeyman-engine-$target$exe"
cp "$server" "$out/journeyman-server-$target$exe"

cli="$staging/journeyman-cli-$target"
mkdir -p "$cli"
cp "$jm" "$engine" "$server" LICENSE "$cli/"
archive "journeyman-cli-$target" "$cli"

if [[ "$os" == darwin ]]; then
  # A zip made by ditto keeps the bundle's signature and symlinks intact.
  ditto -c -k --norsrc --noextattr --keepParent "$app" "$out/journeyman-editor-$target.zip"
else
  app="$staging/journeyman-editor-$target"
  mkdir -p "$app"
  cp "$editor" "$jm" "$engine" "$server" LICENSE "$app/"
  archive "journeyman-editor-$target" "$app"
fi

echo "Packaged $target:"
ls -1 "$out" | grep -- "-$target" | sed 's/^/  /'
