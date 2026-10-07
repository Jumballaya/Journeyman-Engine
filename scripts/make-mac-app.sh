#!/usr/bin/env bash
# Assembles "Journeyman Editor.app" from built binaries and ad-hoc signs it.
#   scripts/make-mac-app.sh <editor> <engine> <jm> <out.app> [version]
# jm and the engine go inside, beside the editor, where it finds them.
set -euo pipefail
cd "$(dirname "$0")/.."

editor="$1" engine="$2" jm="$3" app="$4" version="${5:-0.0.0}"
version="${version#v}"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$editor" "$engine" "$jm" "$app/Contents/MacOS/"

# App icon from editor/icon.png.
iconset="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$iconset"
for size in 16 32 64 128 256 512; do
  sips -z $size $size editor/icon.png --out "$iconset/icon_${size}x${size}.png" >/dev/null
  sips -z $((size * 2)) $((size * 2)) editor/icon.png --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Journeyman Editor</string>
  <key>CFBundleDisplayName</key><string>Journeyman Editor</string>
  <key>CFBundleExecutable</key><string>journeyman_editor</string>
  <key>CFBundleIdentifier</key><string>com.journeyman.editor</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
codesign --force --deep --sign - "$app"
