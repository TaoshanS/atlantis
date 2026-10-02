#!/usr/bin/env bash
# Builds the double-clickable macOS app from your copy of the game and leaves it in dist/:
#
#   tools/make_mac_app.sh              this Mac's architecture
#   UNIVERSAL=1 tools/make_mac_app.sh  arm64 + x86_64
#   ZIP=1 tools/make_mac_app.sh        also write dist/AtlantisSquareOff-mac.zip
#
# Same as `cmake --preset macos && cmake --build --preset macos` (see docs/building.md). The game files go in game/ (game/README.md);
# the assets are extracted automatically. The resulting app contains Nickelodeon's assets: it is for your own use, do not share it.
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(uname)" = "Darwin" ] || { echo "This script must run on macOS." >&2; exit 1; }
PRESET=macos
[ "${UNIVERSAL:-0}" = "1" ] && PRESET=macos-universal
APP="Atlantis SquareOff.app"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" -j "$(sysctl -n hw.ncpu)"
[ "${SKIP_TESTS:-0}" = "1" ] || ctest --preset macos --output-on-failure

mkdir -p dist
rm -rf "dist/$APP"
ditto "build/$PRESET/$APP" "dist/$APP"
xattr -cr "dist/$APP" 2>/dev/null || true
if [ "${ZIP:-0}" = "1" ]; then
    (cd dist && ditto -c -k --keepParent "$APP" AtlantisSquareOff-mac.zip)
fi
echo
echo "Done: dist/$APP"
echo "Open it with:  open \"dist/$APP\""
