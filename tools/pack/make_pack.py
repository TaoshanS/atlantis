#!/usr/bin/env python3
"""Packs the extracted assets (and the game's maps/ XML) into one file the game can mount: sbso --extracted game.pak.

usage: make_pack.py <extracted_dir> <maps_dir> <out.pak>
Files keep their relative path; the maps land under maps/. Data is stored uncompressed (PNG/WAV are already dense enough and mmap-free reads stay cheap).
"""
import os, struct, sys


def main():
    if len(sys.argv) != 4:
        print(__doc__); return 2
    src, maps, out = sys.argv[1:]
    files = []
    for base, prefix in ((src, ""), (maps, "maps/")):
        for d, _, names in os.walk(base):
            for n in names:
                full = os.path.join(d, n)
                rel = prefix + os.path.relpath(full, base).replace(os.sep, "/")
                files.append((rel, full))
    files.sort()
    index = []
    with open(out, "wb") as f:
        f.write(b"SBSOPAK1" + struct.pack("<IQQ", len(files), 0, 0))
        for rel, full in files:
            off = f.tell()
            with open(full, "rb") as g:
                data = g.read()
            f.write(data)
            index.append((rel, off, len(data)))
        idx_off = f.tell()
        for rel, off, size in index:
            nb = rel.encode()
            f.write(struct.pack("<H", len(nb)) + nb + struct.pack("<QQ", off, size))
        idx_size = f.tell() - idx_off
        f.seek(12)
        f.write(struct.pack("<QQ", idx_off, idx_size))
    print(f"{len(files)} files -> {out}")


if __name__ == "__main__":
    sys.exit(main() or 0)
