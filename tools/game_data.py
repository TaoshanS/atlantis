#!/usr/bin/env python3
"""Builds the game data from YOUR copy of SpongeBob Atlantis SquareOff: assets, game.pak and app icons.

  python3 tools/game_data.py [--game DIR] [--main-swf FILE] [--extracted DIR] [--pak FILE] [--icons DIR] [--check]

Inputs (see game/README.md):
  --game      the installed game folder (data/, maps/, sbso.exe ...). Default: ./game, or the legacy
              "./SpongeBob Atlantis SquareOff - WildGames".
  --main-swf  the main SWF dumped from the running game with tools/dump (it is encrypted inside sbso.exe).
              Default: <game>/sbso_main.swf, or the legacy ./recovered/sbso_main_candidate.swf.
Outputs (all git-ignored: they are Nickelodeon's property and must never be shared):
  --extracted the extractor output (default ./extracted). Re-extracted only when the extractor changed.
  --pak       single-file data pack the game mounts (default: not written).
  --icons     app icons (default: not written).
  --check     only validate the inputs and print what was found.

CMake runs this on every build (target game_data); it does nothing when everything is up to date.
"""
import argparse
import hashlib
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEGACY_GAME = ROOT / "SpongeBob Atlantis SquareOff - WildGames"
LEGACY_MAIN = ROOT / "recovered" / "sbso_main_candidate.swf"
# The main SWF of the WildGames / Big Fish release (2008-01-22 sbso.exe), as dumped by tools/dump.
KNOWN_MAIN = {"0185333a129cc29712b1f7ba94c16ad6497325146490b87c679f686628515edc": "WildGames/Big Fish release (2008)"}
REQUIRED = ["data/titlescreen.swf", "data/sound_library.swf", "data/music_library.swf", "maps/SBSO2_CONFIG.xml"]


def fail(msg):
    sys.exit("game_data: " + msg)


def find_game(arg):
    for c in ([Path(arg)] if arg else [ROOT / "game", LEGACY_GAME]):
        if all((c / r).exists() for r in REQUIRED):
            return c.resolve()
    where = arg or "game/"
    missing = [r for r in REQUIRED if not (Path(where) / r).exists()]
    fail("the game files were not found in %s (missing: %s).\n"
         "Copy the folder of your installed game there; see game/README.md." % (where, ", ".join(missing)))


def find_main(arg, game):
    for c in ([Path(arg)] if arg else [game / "sbso_main.swf", LEGACY_MAIN]):
        if c.is_file():
            return c.resolve()
    fail("the main SWF was not found (%s).\n"
         "It is encrypted inside sbso.exe: dump it from the running game with tools/dump (see docs/dumping.md)\n"
         "and save it as game/sbso_main.swf." % (arg or game / "sbso_main.swf"))


def check_main(path):
    data = path.read_bytes()
    if data[:3] not in (b"FWS", b"CWS") or data[3] < 9:
        fail("%s is not an AS3 SWF (header %r)." % (path, data[:4]))
    digest = hashlib.sha256(data).hexdigest()
    if digest in KNOWN_MAIN:
        print("main SWF: %s (%s)" % (path.name, KNOWN_MAIN[digest]))
    else:
        print("WARNING: %s is not the known dump (sha256 %s...). It may be another release or an incomplete dump;"
              " continuing anyway." % (path, digest[:16]))


def run(*args):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([sys.executable, *map(str, args)], check=True)


def newest(paths):
    t = 0.0
    for p in paths:
        for d, _, names in os.walk(p):
            for n in names:
                t = max(t, os.path.getmtime(os.path.join(d, n)))
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game")
    ap.add_argument("--main-swf")
    ap.add_argument("--extracted", default=str(ROOT / "extracted"))
    ap.add_argument("--pak")
    ap.add_argument("--icons")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    game = find_game(a.game)
    main_swf = find_main(a.main_swf, game)
    print("game files: %s" % game)
    check_main(main_swf)
    missing = [m for m in ("PIL", "numpy", "fontTools") if importlib.util.find_spec(m) is None]
    if missing:
        fail("missing python modules: %s -> pip install -r requirements.txt" % ", ".join(missing))
    if a.check:
        return

    ex = Path(a.extracted).resolve()  # absolute: build_assets runs its steps from tools/extract
    run(ROOT / "tools/extract/build_assets.py", "--game", game, "--main-swf", main_swf, "--out", ex, "--if-stale")
    stamp = ex / ".extractor_hash"
    if a.pak:
        pak = Path(a.pak).resolve()
        maps = game / "maps"
        if not pak.exists() or pak.stat().st_mtime < max(stamp.stat().st_mtime, newest([maps])):
            pak.parent.mkdir(parents=True, exist_ok=True)
            run(ROOT / "tools/pack/make_pack.py", ex, maps, pak)
        else:
            print("%s is up to date" % pak)
    if a.icons:
        icons = Path(a.icons).resolve()
        if not (icons / "icon_macos_1024.png").exists() or (icons / "icon_macos_1024.png").stat().st_mtime < stamp.stat().st_mtime:
            run(ROOT / "tools/icon/make_icon.py", ex, icons)


if __name__ == "__main__":
    main()
