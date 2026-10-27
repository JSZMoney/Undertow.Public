#!/bin/bash
# Installs (or updates) the latest Undertow build into your plugin folders.
#
#   curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-mac.sh | bash
#
# What it does — and nothing else:
#   1. downloads the newest build from this repo's "latest" release
#   2. replaces Undertow.vst3 in ~/Library/Audio/Plug-Ins/VST3
#      and Undertow.component in ~/Library/Audio/Plug-Ins/Components
#   3. clears macOS's download-quarantine flag on those two files only
set -euo pipefail

URL="https://github.com/JSZMoney/Undertow.Public/releases/download/latest/Undertow-macOS-plugins.zip"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

if pgrep -f "Ableton Live|Adobe Audition" >/dev/null 2>&1; then
    echo "Please quit Ableton Live / Adobe Audition first, then run this again."
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading the latest Undertow build..."
curl -fsSL "$URL" -o "$TMP/undertow.zip"
ditto -x -k "$TMP/undertow.zip" "$TMP/x"

mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/Undertow.vst3" "$AU_DIR/Undertow.component"
cp -R "$TMP/x/Undertow.vst3" "$VST3_DIR/"
cp -R "$TMP/x/Undertow.component" "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/Undertow.vst3" "$AU_DIR/Undertow.component" 2>/dev/null || true

# Ask macOS to re-read Audio Units so the new version is picked up (it restarts itself)
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo "Undertow installed. Open your DAW and rescan plug-ins."
