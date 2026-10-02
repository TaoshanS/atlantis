# iPhone / iPad

The iOS app is the same code as the desktop game; SDL3 provides the window, Metal rendering, touch and audio. Status: the project and the
platform code are ready, but it **has not been built or run on a device yet** (it needs full Xcode). Expect rough edges; see the checklist
at the end.

## Requirements

* A Mac with **Xcode 15 or later** (the full app, not only the command line tools) and CMake 3.21+.
* Your game files in `game/` and the assets extracted at least once (`cmake --preset macos && cmake --build --preset macos`, or
  `python3 tools/game_data.py --pak build/game.pak --icons build/icons`).
* To install on your own iPhone: a free Apple ID is enough (the app then has to be re-signed every 7 days); a paid developer account
  removes that limit.

## Build

```sh
cmake --preset ios -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<your team id>    # or: --preset ios-simulator
open build/ios/sbso.xcodeproj
```

In Xcode pick the `sbso` scheme and your device (or a simulator) and press Run. Or from the command line:

```sh
cmake --build --preset ios-simulator
xcrun simctl install booted build/ios-simulator/Debug-iphonesimulator/AtlantisSquareOff.app
```

Or in one step, producing `dist/AtlantisSquareOff.ipa` and installing it on the connected device:

```sh
TEAM=<your team id> INSTALL=1 tools/make_ios_ipa.sh
```

Xcode 26 ships the iOS platform separately: if xcodebuild says "iOS 26.x is not installed", get it in Xcode → Settings → Components or
with `xcodebuild -downloadPlatform iOS`.

The game data (`game.pak`, ~170 MB) and the home-screen icons, both made from your game files, are copied into the app bundle at build time.
Such an app contains Nickelodeon's assets: install it on your own devices only, never distribute it.

Your team id is in Xcode → Settings → Accounts, or in the Apple developer portal. Change `SBSO_BUNDLE_ID` if the default identifier
is taken (`-DSBSO_BUNDLE_ID=com.yourname.sbso`).

## What the iOS build does differently

* Landscape only, full screen, status bar hidden, black launch screen (`platforms/ios/`).
* No QUIT button on the title screen (iOS apps do not quit themselves) and no windowed mode.
* Touch: the first finger drives the pointer; hover effects (brackets, enemy health) appear while the finger is down and are cleared when
  it lifts. Text entry for profile names opens the on-screen keyboard.
* The battle scene extends sideways on wide screens; the safe-area insets (notch, home indicator) are read from SDL and passed to the
  layout, but the HUD and menus have not been checked against them yet.
* Saves go to the app's data folder; going to the background saves a snapshot of the battle turn.
* `game.pak` can also be placed in the app's Documents folder through the Files app (useful while developing).

## Checklist for the first device runs

- [ ] Build with Xcode (simulator, then device) and fix whatever the compiler finds.
- [ ] Touch: every button reachable; hover-only information still discoverable; drag on the map and the chest.
- [ ] Safe area on notched iPhones in both landscape orientations; iPad layouts (4:3).
- [ ] Performance and memory: the renderer caches Lanczos-resampled textures; measure on an older iPhone.
- [ ] Audio interruptions (calls, other apps), background/foreground and snapshot resume.
- [ ] App icon (asset catalog) for App Store-style installs.
