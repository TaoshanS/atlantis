#!/usr/bin/env bash
# Builds the iOS app from your copy of the game and packs it as dist/AtlantisSquareOff.ipa (development-signed), optionally installing it
# on a connected iPhone/iPad:
#
#   TEAM=<team id> tools/make_ios_ipa.sh                 build + .ipa
#   TEAM=<team id> INSTALL=1 tools/make_ios_ipa.sh       ... and install on the connected device (devicectl)
#
# Needs full Xcode with the iOS platform (Xcode > Settings > Components) and an Apple developer team whose profile includes the device.
# The .ipa contains Nickelodeon's assets (game.pak): install it on your own devices only. See docs/ios.md.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${TEAM:?set TEAM to your Apple developer team id (Xcode > Settings > Accounts)}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
BUILD=build/ios
cmake --preset ios -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TEAM" ${SBSO_BUNDLE_ID:+-DSBSO_BUNDLE_ID=$SBSO_BUNDLE_ID}
xcodebuild -project "$BUILD/sbso.xcodeproj" -scheme sbso -configuration Release -destination 'generic/platform=iOS' \
    -allowProvisioningUpdates DEVELOPMENT_TEAM="$TEAM" build | grep -E "error:|BUILD|warning: .*/src/" || true
APP="$BUILD/Release-iphoneos/AtlantisSquareOff.app"
[ -d "$APP" ] || { echo "build failed: $APP not found" >&2; exit 1; }
codesign --verify --deep --strict "$APP"

mkdir -p dist
STAGE="$(mktemp -d)"
mkdir -p "$STAGE/Payload"
ditto "$APP" "$STAGE/Payload/AtlantisSquareOff.app"
rm -f dist/AtlantisSquareOff.ipa
(cd "$STAGE" && zip -qry "$OLDPWD/dist/AtlantisSquareOff.ipa" Payload)
rm -rf "$STAGE"
echo "Done: dist/AtlantisSquareOff.ipa"

if [ "${INSTALL:-0}" = "1" ]; then
    DEVICE="${DEVICE:-$(xcrun devicectl list devices 2>/dev/null | grep -E 'available \(paired\)|connected' | grep -oE '[0-9A-F]{8}-([0-9A-F]{4}-){3}[0-9A-F]{12}' | head -1)}"
    [ -n "$DEVICE" ] || { echo "no paired device available" >&2; exit 1; }
    xcrun devicectl device install app --device "$DEVICE" "$APP"
    echo "Installed on $DEVICE"
fi
