#!/usr/bin/env python3
"""Phase 1 extractor: bitmaps, MP3 sounds and symbol names from the original SWFs.

Usage: extract.py <game-dir> <out-dir> [main-swf [only-key]]
Writes out-dir/characters/<swf>.json (shapes, sprites, buttons, texts, fonts, root timeline),
out-dir/images/<swf>/<id>.png, out-dir/sounds/<swf>/<id>.mp3 and
out-dir/manifest.json. Output is Nickelodeon's property: never commit it.
Bitmap alpha is stored straight (un-premultiplied); resample premultiplied.
"""
import io
import json
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

import characters as C
import morph
import framescripts
from swf import cstring, header_info, read_swf

DEFINE_BITS, JPEG_TABLES, DEFINE_BITS_JPEG2 = 6, 8, 21
DEFINE_BITS_LOSSLESS, DEFINE_BITS_JPEG3, DEFINE_BITS_LOSSLESS2 = 20, 35, 36
DEFINE_SOUND, SYMBOL_CLASS = 14, 76
SOUND_RATES = (5512, 11025, 22050, 44100)


def unpremultiply(rgba):
    a = rgba[..., 3:4].astype(np.float32)
    rgb = rgba[..., :3].astype(np.float32)
    out = np.where(a > 0, np.minimum(rgb * 255.0 / np.maximum(a, 1), 255.0), 0)
    return np.concatenate([np.rint(out), a], axis=-1).astype(np.uint8)


def lossless(data, has_alpha):
    cid, fmt, w, h = struct.unpack_from("<HBHH", data, 0)
    pos = 7
    ncolors = 0
    if fmt == 3:
        ncolors = data[pos] + 1
        pos += 1
    raw = zlib.decompress(data[pos:])
    if fmt == 3:
        cs = 4 if has_alpha else 3
        table = np.frombuffer(raw[:ncolors * cs], np.uint8).reshape(ncolors, cs)
        stride = (w + 3) & ~3
        idx = np.frombuffer(raw[ncolors * cs:], np.uint8).reshape(h, stride)[:, :w]
        pix = table[idx]
        if not has_alpha:
            pix = np.concatenate([pix, np.full((h, w, 1), 255, np.uint8)], axis=-1)
        else:
            pix = unpremultiply(pix)
    elif fmt == 5:
        arr = np.frombuffer(raw, np.uint8).reshape(h, w, 4)
        if has_alpha:
            pix = unpremultiply(np.concatenate([arr[..., 1:], arr[..., :1]], axis=-1))
        else:
            pix = np.concatenate([arr[..., 1:], np.full((h, w, 1), 255, np.uint8)], axis=-1)
    else:
        raise ValueError("unsupported lossless format %d" % fmt)
    return cid, Image.fromarray(pix, "RGBA")


def bleed_edges(img, n):
    """The "new card" yellow pattern has a 3 px transparent rim with an antialiased edge that lets the background show as a
    thin line next to the bamboo frame; the frame hides it in the original. Make the rim opaque with the nearest inner colour."""
    a = np.array(img)
    h, w = a.shape[:2]
    for i in range(n - 1, -1, -1):
        a[:, i] = a[:, n]
        a[:, w - 1 - i] = a[:, w - 1 - n]
    for i in range(n - 1, -1, -1):
        a[i] = a[n]
        a[h - 1 - i] = a[h - 1 - n]
    return Image.fromarray(a, "RGBA")


def strip_jpeg(b):
    # Old SWFs may carry erroneous header bytes before the SOI marker.
    if b[:4] == b"\xff\xd9\xff\xd8":
        b = b[4:]
    return b


def jpeg(data, tables, with_alpha):
    cid = struct.unpack_from("<H", data, 0)[0]
    pos = 2
    alpha_off = 0
    if with_alpha:
        alpha_off = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        body = data[pos:pos + alpha_off]
    else:
        body = data[pos:]
    body = strip_jpeg(body)
    if tables is not None and body[:2] == b"\xff\xd8" and b"\xff\xdb" not in body[:200]:
        body = tables[:-2] + body[2:]
    img = Image.open(io.BytesIO(body)).convert("RGB")
    if with_alpha:
        a = np.frombuffer(zlib.decompress(data[pos + alpha_off:]), np.uint8)
        a = a.reshape(img.height, img.width)
        rgba = np.concatenate([np.asarray(img, np.uint8), a[..., None]], axis=-1)
        # Flash's exporter bakes the alpha into the JPEG colours (premultiplied); undo it so edges keep their colour.
        part = (a > 0) & (a < 255)
        if part.sum() > 16 and np.mean(rgba[..., :3].max(-1)[part].astype(int) <= a[part].astype(int) + 8) > 0.97:
            rgba = unpremultiply(rgba)
        img = Image.fromarray(rgba, "RGBA")
    return cid, img


def sound(data):
    cid, flags = struct.unpack_from("<HB", data, 0)
    fmt, rate, bits, stereo = flags >> 4, SOUND_RATES[(flags >> 2) & 3], 16 if flags & 2 else 8, (flags & 1) + 1
    samples = struct.unpack_from("<I", data, 3)[0]
    return cid, dict(format=fmt, rate=rate, channels=stereo, samples=samples), data[7:]


def main(game_dir, out_dir, main_swf=None, only=None):
    """`main_swf` (the recovered main SWF) is extracted too, under the key "main"; `only` limits the run to one key."""
    game, out = Path(game_dir), Path(out_dir)
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if only and manifest_path.exists() else {}
    jobs = []
    for swf_path in sorted(game.rglob("*.swf")):
        rel = swf_path.relative_to(game / "data") if "data" in swf_path.parts else swf_path.relative_to(game)
        jobs.append((swf_path, str(rel.with_suffix(""))))
    if main_swf:
        jobs.append((Path(main_swf), "main"))
    for swf_path, key in jobs:
        if only and key != only:
            continue
        _, _, _, tags = read_swf(swf_path)
        tables = None
        entry = {"images": {}, "sounds": {}, "symbols": {}}
        for t in tags:
            if t.code == JPEG_TABLES:
                tables = t.data
        for t in tags:
            img = None
            if t.code == DEFINE_BITS_LOSSLESS or t.code == DEFINE_BITS_LOSSLESS2:
                cid, img = lossless(t.data, t.code == DEFINE_BITS_LOSSLESS2)
            elif t.code == DEFINE_BITS:
                cid, img = jpeg(t.data, tables, False)
            elif t.code == DEFINE_BITS_JPEG2:
                cid, img = jpeg(t.data, None, False)
            elif t.code == DEFINE_BITS_JPEG3:
                cid, img = jpeg(t.data, None, True)
            elif t.code == DEFINE_SOUND:
                cid, info, payload = sound(t.data)
                d = out / "sounds" / key
                d.mkdir(parents=True, exist_ok=True)
                if info["format"] == 2:  # MP3: skip 2-byte seek-samples field
                    (d / ("%d.mp3" % cid)).write_bytes(payload[2:])
                else:
                    (d / ("%d.raw" % cid)).write_bytes(payload)
                entry["sounds"][cid] = info
            elif t.code == SYMBOL_CLASS:
                n = struct.unpack_from("<H", t.data, 0)[0]
                pos = 2
                for _ in range(n):
                    cid = struct.unpack_from("<H", t.data, pos)[0]
                    name, pos = cstring(t.data, pos + 2)
                    entry["symbols"][cid] = name
            if img is not None and key == "GUI" and cid == 65:
                img = bleed_edges(img, 8)
            if img is not None:
                d = out / "images" / key
                d.mkdir(parents=True, exist_ok=True)
                img.save(d / ("%d.png" % cid))
                entry["images"][cid] = [img.width, img.height]
        chars = {"shapes": {}, "sprites": {}, "buttons": {}, "texts": {}, "fonts": {}, "statics": {}}
        morphs = {}
        for t in tags:
            if t.code in C.SHAPE_TAGS:
                cid, v = C.parse_shape(t); chars["shapes"][cid] = v
            elif t.code == C.DEFINE_SPRITE:
                cid, v = C.parse_sprite(t); chars["sprites"][cid] = v
            elif t.code == 34:
                cid, v = C.parse_button2(t); chars["buttons"][cid] = v
            elif t.code == 37:
                cid, v = C.parse_edit_text(t); chars["texts"][cid] = v
            elif t.code == 75:
                cid, v = C.parse_font3(t); chars["fonts"][cid] = v
            elif t.code in (11, 33):
                cid, v = C.parse_static_text(t); chars["statics"][cid] = v
            elif t.code == morph.MORPH_SHAPE:
                cid, v = morph.parse_morph(t); morphs[cid] = v
        chars["root"] = C.parse_timeline(tags)
        morph.apply_to_characters(chars, morphs)
        chars["symbols"] = entry["symbols"]
        chars["header"] = header_info(swf_path)
        scripts = framescripts.extract(t.data for t in tags if t.code == 82)
        by_name = {name: cid for cid, name in entry["symbols"].items()}
        for cls, frames in scripts.items():
            cid = by_name.get(cls)
            if cid == 0:
                chars["root_scripts"] = {str(k): v for k, v in frames.items()}
            elif cid in chars["sprites"]:
                chars["sprites"][cid]["scripts"] = {str(k): v for k, v in frames.items()}
            else:
                chars.setdefault("unmapped_scripts", {})[cls] = {str(k): v for k, v in frames.items()}
        (out / "characters").mkdir(parents=True, exist_ok=True)
        (out / "characters" / (key.replace("/", "__") + ".json")).write_text(json.dumps(chars, separators=(",", ":")))
        manifest[key] = entry
        print("%-28s images=%-5d sounds=%-4d symbols=%d" % (key, len(entry["images"]), len(entry["sounds"]), len(entry["symbols"])))
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
