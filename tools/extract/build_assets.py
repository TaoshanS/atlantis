#!/usr/bin/env python3
"""One-shot asset build: bitmaps/sprites/scripts, fonts (data SWFs + the recovered main SWF) and the original MP3 sounds.

Usage: build_assets.py [--game <dir>] [--main-swf <file>] [--out <dir>]
Defaults match this repository's layout. Needs: pip install pillow numpy fonttools.
The output is Nickelodeon's property and must not be committed (it is in .gitignore).
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def run(*args):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([sys.executable, *map(str, args)], check=True, cwd=HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=str(ROOT / "SpongeBob Atlantis SquareOff - WildGames"))
    ap.add_argument("--main-swf", default=str(ROOT / "recovered" / "sbso_main_candidate.swf"))
    ap.add_argument("--out", default=str(ROOT / "extracted"))
    ap.add_argument("--if-stale", action="store_true", help="do nothing when ./extracted was made by this very version of the extractor")
    a = ap.parse_args()
    out = Path(a.out).resolve()  # the steps run from tools/extract
    stamp = out / ".extractor_hash"
    h = hashlib.sha256()
    for f in sorted(HERE.glob("*.py")):
        h.update(f.name.encode())
        h.update(f.read_bytes())
    digest = h.hexdigest()
    if a.if_stale and (out / "manifest.json").exists() and stamp.exists() and stamp.read_text().strip() == digest:
        print("extracted/ is up to date")
        return
    out.mkdir(parents=True, exist_ok=True)
    a.game, a.main_swf = str(Path(a.game).resolve()), str(Path(a.main_swf).resolve())
    run("extract.py", a.game, out, a.main_swf)
    run("fonts.py", a.game, out)
    # The main SWF holds the dialogue font (Unibody 8 Black, full Latin-1: used as fallback for accents).
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copy(a.main_swf, Path(tmp) / "main.swf")
        run("fonts.py", tmp, out)
    run("webp.py", out)  # bitmaps: PNG -> lossless WebP (same pixels, a third smaller)
    # audio: the game plays the original MP3s (sounds/), decoding and resampling them to 48 kHz itself
    shutil.rmtree(out / "sounds_wav", ignore_errors=True)  # outputs of older extractor versions
    shutil.rmtree(out / "sounds_flac", ignore_errors=True)
    stamp.write_text(digest + "\n")
    print("assets ready in", out)


if __name__ == "__main__":
    main()
