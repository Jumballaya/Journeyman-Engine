#!/bin/sh
# Installs the Journeyman CLI (jm and the engine) from a GitHub release.
#
#   curl -fsSL https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/install.sh | sh
#
# JM_VERSION=v0.0.1   a release (default: the one this script came with)
# JM_INSTALL_DIR=dir  where it goes (default: ~/.jm); jm lands in <dir>/bin
#
# Never prompts and never edits shell profiles: it prints the PATH line to add.
# macOS and Linux; on Windows, unzip journeyman-cli-windows-amd64.zip instead.
set -eu

repo="https://github.com/Jumballaya/Journeyman-Engine/releases"
version="${JM_VERSION:-latest}"
dir="${JM_INSTALL_DIR:-$HOME/.jm}"

fail() { echo "install: $*" >&2; exit 1; }

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

command -v curl >/dev/null 2>&1 || fail "needs curl"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "Downloading $file ($version)"
curl -fsSL "$base/$file" -o "$tmp/$file" || fail "couldn't download $base/$file"
curl -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS" || fail "couldn't download $base/SHA256SUMS"

expected="$(grep " $file\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)"
[ -n "$expected" ] || fail "SHA256SUMS has no entry for $file"
if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$tmp/$file" | cut -d' ' -f1)"
else actual="$(shasum -a 256 "$tmp/$file" | cut -d' ' -f1)"; fi
[ "$actual" = "$expected" ] || fail "checksum mismatch for $file: the download is damaged or not the release's"

tar -xzf "$tmp/$file" -C "$tmp"
# Swapped in whole, so a failed install leaves the previous one working.
mkdir -p "$dir"
rm -rf "$dir/bin.new"
mv "$tmp/journeyman-cli-$platform" "$dir/bin.new"
rm -rf "$dir/bin"
mv "$dir/bin.new" "$dir/bin"

echo "Installed $("$dir/bin/jm" --version) in $dir/bin"
case ":$PATH:" in
  *":$dir/bin:"*) ;;
  *) echo "Add it to PATH (and to your shell profile to keep it):"
     echo "  export PATH=\"$dir/bin:\$PATH\"" ;;
esac

echo "Check it with: jm doctor (the first jm build downloads the script compiler if needed)"
