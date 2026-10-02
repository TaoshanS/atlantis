#!/usr/bin/env python3
"""Packs the extracted assets (and the game's maps/ XML) into one file the game can mount: sbso --extracted game.pak.

usage: make_pack.py <extracted_dir> <maps_dir> <out.pak>
Files keep their relative path; the maps land under maps/.
Format "SBSOPAK2" (read by src/core/vfs.cpp): identical files are stored once (their index entries share the offset), and entries
that zlib shrinks by at least 10 % (the JSON and XML) are stored compressed. WebP and MP3 are already compressed and stay as they are.
"""
import hashlib
import os
import struct
import sys
import zlib

MAGIC = b"SBSOPAK2"
FLAG_ZLIB = 1


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    src, maps, out = sys.argv[1:]
    files = []
    # not needed at run time: audio converted by older extractor versions (the game plays the original MP3s in sounds/)
    skip = {os.path.join(src, "sounds_flac"), os.path.join(src, "sounds_wav")}
    for base, prefix in ((src, ""), (maps, "maps/")):
        for d, dirs, names in os.walk(base):
            dirs[:] = [x for x in dirs if os.path.join(d, x) not in skip]
            for n in names:
                if n.startswith("."):  # .extractor_hash and the like
                    continue
                full = os.path.join(d, n)
                rel = prefix + os.path.relpath(full, base).replace(os.sep, "/")
                files.append((rel, full))
    files.sort()
    index, stored = [], {}  # stored: sha1 of the raw data -> (offset, stored size, raw size, flags)
    raw_total = written = shared = 0
    with open(out + ".tmp", "wb") as f:
        f.write(MAGIC + struct.pack("<IQQ", len(files), 0, 0))
        for rel, full in files:
            with open(full, "rb") as g:
                data = g.read()
            raw_total += len(data)
            key = hashlib.sha1(data).digest()
            if key in stored:
                shared += 1
            else:
                payload, flags = data, 0
                if len(data) > 256:
                    z = zlib.compress(data, 9)
                    if len(z) <= len(data) * 0.9:
                        payload, flags = z, FLAG_ZLIB
                stored[key] = (f.tell(), len(payload), len(data), flags)
                f.write(payload)
                written += len(payload)
            index.append((rel,) + stored[key])
        idx_off = f.tell()
        for rel, off, size, raw, flags in index:
            nb = rel.encode()
            f.write(struct.pack("<H", len(nb)) + nb + struct.pack("<QQQB", off, size, raw, flags))
        idx_size = f.tell() - idx_off
        f.seek(12)
        f.write(struct.pack("<QQ", idx_off, idx_size))
    os.replace(out + ".tmp", out)
    print(f"{len(files)} files ({shared} duplicates stored once), {raw_total / 1e6:.1f} MB -> {written / 1e6:.1f} MB: {out}")


if __name__ == "__main__":
    sys.exit(main() or 0)
