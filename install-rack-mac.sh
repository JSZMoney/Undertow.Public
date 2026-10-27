#!/bin/bash
# Installs (or updates) the latest Undertow Rack build into your own Applications folder.
#
#   curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-rack-mac.sh | bash
#
# What it does, and nothing else:
#   1. downloads the newest build from this repo's "rack-latest" release
#   2. replaces "Undertow Rack.app" in ~/Applications (no admin rights, no drivers)
#   3. clears macOS's download-quarantine flag on that app only
#
# To remove it completely:
#   curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-rack-mac.sh | bash -s -- --uninstall
set -euo pipefail

URL="https://github.com/JSZMoney/Undertow.Public/releases/download/rack-latest/UndertowRack-macOS.zip"
APP_DIR="$HOME/Applications"
APP="$APP_DIR/Undertow Rack.app"
SETTINGS="$HOME/Library/Application Support/Undertow Rack"

if pgrep -x "Undertow Rack" >/dev/null 2>&1; then
    echo "Please quit Undertow Rack first, then run this again."
    exit 1
fi

if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$APP" "$SETTINGS"
    echo "Undertow Rack and its settings are removed."
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading the latest Undertow Rack build..."
curl -fsSL "$URL" -o "$TMP/rack.zip"
ditto -x -k "$TMP/rack.zip" "$TMP/x"

mkdir -p "$APP_DIR"
rm -rf "$APP"
cp -R "$TMP/x/Undertow Rack.app" "$APP_DIR/"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "Undertow Rack installed in $APP_DIR. Opening it now..."
open "$APP"
