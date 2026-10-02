# Your game files go here

This folder is ignored by git: nothing you put here is ever committed. The port does **not** include any of the game's art, audio or
text; it is built from your own copy of the original PC game, *SpongeBob SquarePants: Atlantis SquareOff* (2008).

Put here:

```
game/
├── data/            ← from your installed game folder
├── maps/            ←   "
├── pics/, sbso.exe, …   (the rest of the install folder can come along; it is ignored)
└── sbso_main.swf    ← the main SWF dumped from the running game (see docs/dumping.md)
```

1. Copy **the contents** of the folder where the game is installed (the one with `sbso.exe`, `data/` and `maps/`) into `game/`.
2. Dump the main SWF with `tools/dump` (it is encrypted inside `sbso.exe`, so it can only be read from memory while the game runs)
   and save it as `game/sbso_main.swf`. Step by step: [docs/dumping.md](../docs/dumping.md).
3. Build (see the main README). The assets are extracted automatically on the first build; check the inputs any time with
   `python3 tools/game_data.py --check`.

The extracted assets, `game.pak` and the app icons are made from these files: they are for your own use only and must not be shared.
