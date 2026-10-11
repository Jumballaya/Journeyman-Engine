#!/bin/sh
# Installs the Journeyman CLI (jm and the engine) from a GitHub release.
#
#   curl -fsSL https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/install.sh | sh
#   ... | sh -s -- --editor     the editor too: ~/Applications on macOS, <dir>/editor on Linux
#
# JM_VERSION=v0.0.1   a release (default: the one this script came with)
# JM_INSTALL_DIR=dir  where it goes (default: ~/.jm); jm lands in <dir>/bin
# JM_FROM=dir         install from release files already in dir (CI, offline)
#
# Everything it does is logged to <dir>/install.log, printed when it fails.
# Never prompts and never edits shell profiles: it prints the PATH line to add.
# macOS and Linux; on Windows, unzip journeyman-cli-windows-amd64.zip instead.
set -eu

repo="https://github.com/Jumballaya/Journeyman-Engine/releases"
version="${JM_VERSION:-latest}"
dir="${JM_INSTALL_DIR:-$HOME/.jm}"
editor=no
for arg in "$@"; do
  case "$arg" in
    --editor) editor=yes ;;
    *) echo "install: unknown option $arg (there's --editor)" >&2; exit 2 ;;
  esac
done

mkdir -p "$dir"
dir="$(cd "$dir" && pwd)"
log="$dir/install.log"
: >"$log"
# Every error goes to the log from here; any failed exit prints the log.
exec 3>&2 2>>"$log"
tmp=""
trap 'st=$?; rm -rf "$tmp" || :; [ "$st" = 0 ] || { echo "--- $log:"; cat "$log"; } >&3; exit "$st"' EXIT
say() { echo "$*"; echo "$*" >>"$log"; }
fail() { echo "install: $*" >&2; exit 1; }
say "install.sh $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -srm), into $dir"

case "$(uname -s)" in
  Darwin) os=darwin ;;
  Linux) os=linux ;;
  *) fail "$(uname -s) isn't supported here; on Windows, download journeyman-cli-windows-amd64.zip from $repo" ;;
esac
case "$(uname -m)" in
  arm64 | aarch64) arch=arm64 ;;
  x86_64 | amd64) arch=amd64 ;;
  *) fail "no build for a $(uname -m) CPU" ;;
esac
platform="$os-$arch"
[ "$platform" = linux-arm64 ] && fail "no linux-arm64 build yet (there are darwin-arm64, darwin-amd64 and linux-amd64)"

if [ "$version" = latest ]; then base="$repo/latest/download"; else base="$repo/download/$version"; fi
file="journeyman-cli-$platform.tar.gz"

tmp="$(mktemp -d)"

# fetch puts one of the release's files in $tmp, checked against SHA256SUMS.
fetch() {
  if [ -n "${JM_FROM:-}" ]; then
    say "Copying $1 from $JM_FROM"
    cp "$JM_FROM/$1" "$JM_FROM/SHA256SUMS" "$tmp/" || fail "$JM_FROM needs $1 and SHA256SUMS"
  else
    command -v curl >/dev/null 2>&1 || fail "needs curl"
    say "Downloading $1 ($version)"
    curl -fsSL "$base/$1" -o "$tmp/$1" || fail "couldn't download $base/$1"
    curl -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS" || fail "couldn't download $base/SHA256SUMS"
  fi
  expected="$(grep " $1\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)"
  [ -n "$expected" ] || fail "SHA256SUMS has no entry for $1"
  if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$tmp/$1" | cut -d' ' -f1)"
  else actual="$(shasum -a 256 "$tmp/$1" | cut -d' ' -f1)"; fi
  [ "$actual" = "$expected" ] || fail "checksum mismatch for $1: the download is damaged or not the release's"
}

# swap replaces folder $2 with $1 whole, so a failed install leaves the old one working.
swap() {
  rm -rf "$2.new"
  mv "$1" "$2.new" || fail "couldn't write $2"
  rm -rf "$2"
  mv "$2.new" "$2"
}

fetch "$file"

tar -xzf "$tmp/$file" -C "$tmp" || fail "couldn't unpack $file"
[ -x "$tmp/journeyman-cli-$platform/jm" ] || fail "$file doesn't hold journeyman-cli-$platform/jm"
swap "$tmp/journeyman-cli-$platform" "$dir/bin"

if [ "$editor" = yes ]; then
  if [ "$os" = darwin ]; then
    fetch "journeyman-editor-$platform.zip"
    unzip -q "$tmp/journeyman-editor-$platform.zip" -d "$tmp/editor" || fail "couldn't unpack the editor"
    mkdir -p "$HOME/Applications"
    swap "$tmp/editor/Journeyman Editor.app" "$HOME/Applications/Journeyman Editor.app"
    say "Installed the editor in ~/Applications (open it there, or: jm editor)"
  else
    fetch "journeyman-editor-$platform.tar.gz"
    tar -xzf "$tmp/journeyman-editor-$platform.tar.gz" -C "$tmp" || fail "couldn't unpack the editor"
    swap "$tmp/journeyman-editor-$platform" "$dir/editor"
    say "Installed the editor in $dir/editor (run it with: jm editor)"
  fi
fi

jm="$dir/bin/jm"
installed="$("$jm" --version)" || fail "the installed jm doesn't run"
say "Installed $installed in $dir/bin"
# doctor's warnings (PATH, the toolchain still to download) are for the user;
# its errors mean this install won't work.
(cd "$dir" && "$jm" doctor) >>"$log" || fail "jm doctor found a problem with this install"
case ":$PATH:" in
  *":$dir/bin:"*) ;;
  *) echo "Add it to PATH (and to your shell profile to keep it):"
     echo "  export PATH=\"$dir/bin:\$PATH\"" ;;
esac

echo "Next: jm setup (connects your agent apps), then jm init in a new folder, or jm editor"
