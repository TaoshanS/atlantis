<div align="center">

# Atlantis SquareOff

**A native port of *SpongeBob SquarePants: Atlantis SquareOff* (2008)**

  <p align="center">
    <a href="docs/building.md">Building</a>
    •
    <a href="docs/dumping.md">Dumping your game</a>
    •
    <a href="docs/ios.md">iPhone / iPad</a>
    •
    <a href="https://github.com/TaoshanS/atlantis/issues">Issues</a>
  </p>
</div>

# Overview

Atlantis SquareOff is a reverse-engineered, native reimplementation of the 2008 Flash/PC turn-based battle game
*SpongeBob SquarePants: Atlantis SquareOff*, written in C++17 with SDL3. The Flash runtime is gone: the original game logic was studied
and rewritten, and the original art, animation, sound and text are read from **your own copy** of the game.

It aims to play exactly like the original while providing new options and enhancements:

- macOS, Windows, Linux and iPhone/iPad, with touch controls
- high-resolution rendering (Lanczos resampling, or the original pixels) and wide screens that show more of the battlefield
- 48 kHz audio, several manual save slots per profile and a battle autosave every turn
- English and Spanish (Spain), and import of the original game's saves

> [!IMPORTANT]
> This project is not affiliated with or endorsed by Nickelodeon, Viacom or the original developers. Its only official home is
> https://github.com/TaoshanS/atlantis.

# Setup

> [!IMPORTANT]
> Atlantis SquareOff does *not* provide any copyrighted assets. You must provide your own copy of the original PC game. Anything built
> from it (extracted assets, `game.pak`, app bundles, `.ipa` files) is for your own use and must not be shared.

> [!IMPORTANT]
> At a minimum, Atlantis SquareOff requires macOS 11, Windows 10, a recent Linux or iOS 15, and a GPU supported by SDL's renderer
> (Metal, Direct3D 11/12, Vulkan or OpenGL).

### 1. Dump your game

You need the PC release of the game (the 2008 WildGames / Big Fish Games download) installed somewhere. Copy its folder into `game/`.

The main movie of the game is encrypted inside `sbso.exe` and only exists in memory while the game runs, so it has to be dumped once:
start the original game on Windows (or in a Windows virtual machine), wait for the title screen and run `sbso_dump.exe` from
[`tools/dump`](tools/dump). Save the result as `game/sbso_main.swf`. Step-by-step instructions: [docs/dumping.md](docs/dumping.md).

```
game/
├── data/  maps/  …     ← your installed game folder
└── sbso_main.swf       ← dumped with tools/dump
```

> [!NOTE]
> Only the WildGames / Big Fish Games release has been verified. Check your files at any time with
> `python3 tools/game_data.py --check`; another release may work but will be reported as unknown.

### 2. Build and install

The assets are extracted from `game/` automatically during the build:

```sh
pip install -r requirements.txt          # Pillow, NumPy, fontTools
cmake --preset macos                     # or: linux, windows, ios
cmake --build --preset macos
```

* **macOS**: `build/macos/Atlantis SquareOff.app` (`tools/make_mac_app.sh` builds it and copies it to `dist/`).
* **iPhone / iPad**: `TEAM=<your team id> INSTALL=1 tools/make_ios_ipa.sh` builds `dist/AtlantisSquareOff.ipa` and installs it on a
  connected device. See the [iOS guide](docs/ios.md).
* **Windows / Linux**: `sbso` plus `game.pak` in the build folder; `cmake --install` on Linux adds a desktop entry.

> [!NOTE]
> Saves live in your user data folder, outside the app, so they survive reinstalls. Profiles of the original game (`SBSO2Profiles.sol`)
> can be imported with `--import-sol <file>`.

# Building

If you'd like to build Atlantis SquareOff from source, please read the [build instructions](docs/building.md).

Pull requests are welcome! Please keep to the style of the surrounding code (`.clang-format`), never commit anything extracted from the
game, and validate gameplay changes against the original (design notes in [`docs/`](docs) and [`AGENTS.md`](AGENTS.md)).

# Credits

*SpongeBob SquarePants: Atlantis SquareOff* was developed by Pop & Company, Tiny Mantis and This Is Pop, tested by iBeta and published by
Nickelodeon. SpongeBob SquarePants and all related titles, logos and characters are trademarks of Viacom International Inc.

Special thanks to the [SDL](https://libsdl.org) developers, [pugixml](https://pugixml.org), [stb](https://github.com/nothings/stb) and
[nlohmann/json](https://github.com/nlohmann/json), the [JPEXS Free Flash Decompiler](https://github.com/jindrapetrik/jpexs-decompiler)
and [Ruffle](https://ruffle.rs) projects, and all [contributors](https://github.com/TaoshanS/atlantis/graphs/contributors).
