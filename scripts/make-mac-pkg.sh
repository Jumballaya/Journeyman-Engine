#!/usr/bin/env bash
# Makes the one-file macOS install: a .pkg that puts "Journeyman Editor.app" in
# /Applications and links jm, the engine and the server into /usr/local/bin
# (into the app, so a newer app updates them too).
#   scripts/make-mac-pkg.sh <Journeyman Editor.app> <out.pkg> [version]
# Signed with JM_INSTALLER_IDENTITY (a Developer ID Installer identity) if set.
set -euo pipefail
cd "$(dirname "$0")/.."

app="$1" out="$2" version="${3:-0.0.0}"
version="${version#v}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/root/Applications" "$work/scripts"
ditto --norsrc --noextattr --noqtn "$app" "$work/root/Applications/Journeyman Editor.app"  # no xattrs as ._ files

cat >"$work/scripts/postinstall" <<'SCRIPT'
#!/bin/sh
# Puts the app's command-line tools on PATH, on the volume installed to ($3).
# A tool that's already there and isn't one of these links is left alone.
app="/Applications/Journeyman Editor.app/Contents/MacOS"
bin="$3/usr/local/bin"
mkdir -p "$bin"
for tool in jm journeyman_engine journeyman_server; do
  if [ -e "$bin/$tool" ] || [ -L "$bin/$tool" ]; then
    case "$(readlink "$bin/$tool")" in
      "$app/$tool") ;;
      *) echo "Journeyman: $bin/$tool exists and isn't ours; not replaced" >&2; continue ;;
    esac
  fi
  ln -sf "$app/$tool" "$bin/$tool"
done
SCRIPT
chmod +x "$work/scripts/postinstall"

# The app is never relocated: it must stay where the links point.
pkgbuild --analyze --root "$work/root" "$work/components.plist" >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$work/components.plist"
pkgbuild --root "$work/root" --component-plist "$work/components.plist" --scripts "$work/scripts" \
  --identifier com.journeyman.editor --version "$version" --install-location / "$work/journeyman.pkg"

signing=()
[[ -n "${JM_INSTALLER_IDENTITY:-}" ]] && signing=(--sign "$JM_INSTALLER_IDENTITY" --timestamp)
productbuild --package "$work/journeyman.pkg" ${signing[@]+"${signing[@]}"} "$out"
