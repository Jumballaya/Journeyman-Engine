#!/bin/sh
# Installs the Journeyman CLI (jm and the engine) from a GitHub release.
#
#   curl -fsSL https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/install.sh | sh
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

if [ -n "${JM_FROM:-}" ]; then
  say "Copying $file from $JM_FROM"
  cp "$JM_FROM/$file" "$JM_FROM/SHA256SUMS" "$tmp/" || fail "$JM_FROM needs $file and SHA256SUMS"
else
  command -v curl >/dev/null 2>&1 || fail "needs curl"
  say "Downloading $file ($version)"
  curl -fsSL "$base/$file" -o "$tmp/$file" || fail "couldn't download $base/$file"
  curl -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS" || fail "couldn't download $base/SHA256SUMS"
fi

expected="$(grep " $file\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)"
[ -n "$expected" ] || fail "SHA256SUMS has no entry for $file"
if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$tmp/$file" | cut -d' ' -f1)"
else actual="$(shasum -a 256 "$tmp/$file" | cut -d' ' -f1)"; fi
[ "$actual" = "$expected" ] || fail "checksum mismatch for $file: the download is damaged or not the release's"

tar -xzf "$tmp/$file" -C "$tmp" || fail "couldn't unpack $file"
[ -x "$tmp/journeyman-cli-$platform/jm" ] || fail "$file doesn't hold journeyman-cli-$platform/jm"
# Swapped in whole, so a failed install leaves the previous one working.
rm -rf "$dir/bin.new"
mv "$tmp/journeyman-cli-$platform" "$dir/bin.new"
rm -rf "$dir/bin"
mv "$dir/bin.new" "$dir/bin"

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

echo "Check it with: jm doctor (the first jm build downloads the script compiler if needed)"
