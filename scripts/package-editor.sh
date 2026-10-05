#!/usr/bin/env bash
# Builds the editor, the engine and jm, and puts them together:
#   macOS: dist/Journeyman Editor.app   (double-click; jm and the engine inside)
#   Linux: dist/journeyman-editor/      (run ./journeyman_editor)
# Building game scripts still needs Node.js on the machine.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release
cmake --build --preset release --target journeyman_engine journeyman_editor
(cd cli && go build -o ../build/bin/jm ./cmd/jm)

mkdir -p dist
if [[ "$(uname)" == "Darwin" ]]; then
  app="dist/Journeyman Editor.app"
  rm -rf "$app"
  mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
  cp build/release/editor/journeyman_editor build/release/engine/journeyman_engine build/bin/jm "$app/Contents/MacOS/"
  cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Journeyman Editor</string>
  <key>CFBundleDisplayName</key><string>Journeyman Editor</string>
  <key>CFBundleExecutable</key><string>journeyman_editor</string>
  <key>CFBundleIdentifier</key><string>com.journeyman.editor</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
  codesign --force --deep --sign - "$app"
  echo "Packaged $app"
else
  out="dist/journeyman-editor"
  rm -rf "$out"
  mkdir -p "$out"
  cp build/release/editor/journeyman_editor build/release/engine/journeyman_engine build/bin/jm "$out/"
  echo "Packaged $out"
fi
