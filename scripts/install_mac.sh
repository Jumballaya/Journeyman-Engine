#!/usr/bin/env bash
# Build the editor, engine and CLI, then install the app in /Applications.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This installer requires macOS." >&2
  exit 1
fi

if [[ "$EUID" -eq 0 ]]; then
  echo "Run this script without sudo; it requests permission for installation if needed." >&2
  exit 1
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."
bash scripts/package-editor.sh

app="$(pwd)/dist/Journeyman Editor.app"
destination="/Applications/Journeyman Editor.app"
privilege=(/usr/bin/env)
if [[ ! -w /Applications ]]; then
  echo "Administrator permission is needed to install in /Applications."
  sudo -v
  privilege=(sudo)
fi

# Finish copying and verifying before replacing an existing installation.
staging="$("${privilege[@]}" mktemp -d /Applications/.journeyman-install.XXXXXX)"
cleanup() {
  local result=$?
  if [[ -d "$staging/previous.app" && ! -e "$destination" ]]; then
    if ! "${privilege[@]}" mv "$staging/previous.app" "$destination"; then
      echo "Could not restore the previous app; it remains at $staging/previous.app" >&2
      return 1
    fi
  fi
  "${privilege[@]}" rm -rf "$staging"
  return "$result"
}
trap cleanup EXIT

"${privilege[@]}" /usr/bin/ditto "$app" "$staging/new.app"
"${privilege[@]}" /usr/bin/codesign --verify --deep --strict "$staging/new.app"
if [[ -e "$destination" ]]; then
  "${privilege[@]}" mv "$destination" "$staging/previous.app"
fi
"${privilege[@]}" mv "$staging/new.app" "$destination"

echo "Installed $destination"
echo "Open Journeyman Editor from Applications."
