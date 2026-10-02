#!/usr/bin/env python3
"""Converts extracted MP3 sounds to 48 kHz mono 16-bit WAV with high-quality sinc resampling.

Usage: audio.py <extracted-dir>   (reads sounds/**/*.mp3, writes sounds_wav/**/*.wav)
No other processing (no denoise, exciter or bandwidth extension): decided by the user.
"""
import subprocess
import sys
from pathlib import Path


def resampler(ffmpeg="ffmpeg"):
    out = subprocess.run([ffmpeg, "-hide_banner", "-buildconf"], capture_output=True, text=True).stdout
    return "aresample=resampler=soxr:precision=28" if "--enable-libsoxr" in out else "aresample=resampler=swr:filter_size=64:phase_shift=15:linear_interp=0"


def main(root):
    root = Path(root)
    flt = resampler()
    n = 0
    for mp3 in sorted((root / "sounds").rglob("*.mp3")):
        wav = root / "sounds_wav" / mp3.relative_to(root / "sounds").with_suffix(".wav")
        wav.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(mp3), "-af", flt,
                        "-ar", "48000", "-ac", "1", "-c:a", "pcm_s16le", str(wav)], check=True)
        n += 1
    print("converted %d sounds with %s" % (n, flt))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
