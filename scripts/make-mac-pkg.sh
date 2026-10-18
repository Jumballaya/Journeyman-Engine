#!/usr/bin/env bash
# Makes the one-file macOS install: a .pkg that puts "Journeyman Editor.app" in
# /Applications and links jm, the engine, the player and the server into /usr/local/bin
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
for tool in jm journeyman_engine journeyman_player journeyman_server; do
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

# The installer: a title, a welcome and a last screen saying what to do next
# (scripts/mac/*.html), the app's minimum macOS, and only the Mac chip it's built for.
chip="$(lipo -archs "$app/Contents/MacOS/journeyman_editor")"
case "$chip" in
  arm64) need="a Mac with Apple silicon" other="an Intel Mac" other_pkg="journeyman-darwin-amd64.pkg" arm=true ;;
  x86_64) need="an Intel Mac" other="a Mac with Apple silicon" other_pkg="journeyman-darwin-arm64.pkg" arm=false ;;
  *) echo "the editor is built for '$chip': one chip, arm64 or x86_64, per .pkg" >&2; exit 1 ;;
esac
min="$(plutil -extract LSMinimumSystemVersion raw "$app/Contents/Info.plist")"
cat >"$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Journeyman</title>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <allowed-os-versions><os-version min="$min"/></allowed-os-versions>
  <installation-check script="rightChip()"/>
  <script><![CDATA[
function rightChip() {
  if ((system.sysctl('hw.optional.arm64') == 1) == $arm) return true;
  my.result.type = 'Fatal';
  my.result.title = 'This installer is for $need';
  my.result.message = 'This is $other: download $other_pkg from the Journeyman download page instead.';
  return false;
}
]]></script>
  <choices-outline><line choice="journeyman"/></choices-outline>
  <choice id="journeyman" title="Journeyman"><pkg-ref id="com.journeyman.editor"/></choice>
  <pkg-ref id="com.journeyman.editor" version="$version" onConclusion="none">#journeyman.pkg</pkg-ref>
</installer-gui-script>
XML

signing=()
[[ -n "${JM_INSTALLER_IDENTITY:-}" ]] && signing=(--sign "$JM_INSTALLER_IDENTITY" --timestamp)
productbuild --distribution "$work/distribution.xml" --resources scripts/mac --package-path "$work" \
  ${signing[@]+"${signing[@]}"} "$out"
