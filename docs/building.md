# Building Atlantis SquareOff

The build has two halves:

1. **Code** — C++17, CMake. SDL3 is used when installed, otherwise downloaded and linked statically on the first configure.
2. **Game data** — made from your copy of the game in `game/` (see [game/README.md](../game/README.md) and [dumping.md](dumping.md)).
   The CMake target `game_data` runs [`tools/game_data.py`](../tools/game_data.py), which extracts the assets into `extracted/`
   (only when the extractor changed), packs them into `game.pak` and makes the app icons. Without game files only the code is built.

## Dependencies

* [CMake 3.21+](https://cmake.org)
* A C++17 compiler (Clang, GCC or MSVC)
* [Python 3.8+](https://python.org) with `pip install -r requirements.txt` (Pillow, NumPy, fontTools)
* Internet access on the first configure if SDL3 is not installed

### macOS

```sh
xcode-select --install          # command line tools (full Xcode only for iOS)
brew install cmake python
pip3 install -r requirements.txt
```

### Linux (Ubuntu 24.04)

```sh
sudo apt install build-essential cmake ninja-build python3-pip pkg-config \
  libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev libudev-dev
pip3 install -r requirements.txt
```
(the X11/Wayland/audio packages are what SDL3 needs when it is built from source; a distribution SDL3 package works too).

### Windows

Visual Studio 2022 with *Desktop development with C++* (it includes CMake), Python 3 from python.org or the Microsoft Store.
Windows support is new: report anything that does not work.

## Configure and build

Presets (in [CMakePresets.json](../CMakePresets.json)) put each build in `build/<preset>/`:

| Preset | Result |
| --- | --- |
| `macos` | `build/macos/Atlantis SquareOff.app` (self-contained, ad-hoc signed) and the `sbso` developer executable |
| `macos-universal` | the same for Apple silicon + Intel |
| `linux` | `build/linux/sbso` + `game.pak`; `cmake --install` adds a `.desktop` entry and icon |
| `windows` | `build/windows/Release/sbso.exe` + `game.pak` |
| `ios`, `ios-simulator` | an Xcode project, see [ios.md](ios.md) |
| `code-only` | code and tests without game data (what CI runs) |

```sh
cmake --preset macos
cmake --build --preset macos
ctest --preset macos
```

Without presets: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.

Useful options (`-D<option>=<value>`):

| Option | Default | |
| --- | --- | --- |
| `SBSO_GAME_DIR` | `game/` | folder with your installed game |
| `SBSO_MAIN_SWF` | `<game>/sbso_main.swf` | the dumped main SWF |
| `SBSO_EXTRACTED_DIR` | `extracted/` | extractor output, shared by all build folders |
| `SBSO_BUILD_GAME_DATA` | ON when the game files are found | OFF builds only the code |
| `SBSO_STATIC_SDL3` | OFF (ON in the macOS presets) | ignore an installed SDL3, download and link it statically |
| `SBSO_BUILD_TESTS` | ON (OFF on iOS) | tests and developer tools |
| `SBSO_BUNDLE_ID` | `io.github.sbso.atlantis` | bundle identifier of the macOS/iOS app |

## Running

* The app bundle (macOS) carries its `game.pak`. The `sbso` executable looks for `game.pak` next to itself, in the per-user data folder
  and in Documents, so `./build/<preset>/sbso` runs straight away.
* `sbso --extracted extracted --maps game/maps` runs from the unpacked assets (handy while working on the extractor).
* Saves live in the per-user data folder (macOS: `~/Library/Application Support/sbso/atlantis/`), or `--saves <dir>`.
* `--import-sol <file>` imports the profiles of the original game (`SBSO2Profiles.sol`).
* Developer options for scripted, headless runs (`--headless N --shot out.png --battle S,N --click x,y@tick …`) are listed in
  [AGENTS.md](../AGENTS.md).

## Repository layout

```
.github/workflows/  CI: builds the code and runs the tests without game data; builds the dumper
cmake/              SDL3, game data and packaging modules
docs/               build, dumping and iOS guides; design notes (Spanish)
game/               your game files (git-ignored, except its README)
platforms/          per-OS packaging: Info.plist templates, launch screen, .desktop entry, Windows resources
src/                the port: core, game logic, saves, engine, audio, i18n, app
tests/              unit and smoke tests
third_party/        pugixml, stb, nlohmann/json, minimp3 (vendored); libwebp and SDL3 are downloaded by CMake
tools/              extractor, pack and icon tools, the SWF dumper, screenshot helpers
```
