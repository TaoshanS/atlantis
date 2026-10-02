#!/usr/bin/env python3
"""Exports DefineFont3 fonts from the SWFs as TrueType files plus a JSON with metrics.

Usage: fonts.py <game-dir> <out-dir>   (writes out-dir/fonts/<name>.ttf and fonts.json)
Glyph outlines are quadratic, so they map 1:1 to TrueType.
"""
import json
import struct
import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

from characters import Bits
from swf import read_swf

EM = 1024   # SWF DefineFont3 uses 1/20480 em; scaled by 1/20 so glyph bounds fit in int16
SCALE = 20


def glyph_contours(data):
    r = Bits(data)
    nf, nl = r.ub(4), r.ub(4)
    contours, cur = [], []
    x = y = 0
    while True:
        if r.ub(1):
            straight = r.ub(1)
            n = r.ub(4) + 2
            if straight:
                if r.ub(1):
                    x += r.sb(n); y += r.sb(n)
                elif r.ub(1):
                    y += r.sb(n)
                else:
                    x += r.sb(n)
                cur.append(("L", x, y))
            else:
                cx, cy = x + r.sb(n), y + r.sb(n)
                x, y = cx + r.sb(n), cy + r.sb(n)
                cur.append(("Q", cx, cy, x, y))
            continue
        flags = r.ub(5)
        if flags == 0:
            break
        if flags & 1:
            if cur:
                contours.append(cur)
            n = r.ub(5)
            x, y = r.sb(n), r.sb(n)
            cur = [("M", x, y)]
        if flags & 2: r.ub(nf)
        if flags & 4: r.ub(nf)
        if flags & 8: r.ub(nl)
    if cur:
        contours.append(cur)
    return contours


def parse_font3(data):
    fid = struct.unpack_from("<H", data, 0)[0]
    flags, _lang, nlen = data[2], data[3], data[4]
    name = data[5:5 + nlen].decode("utf-8", "replace").rstrip("\0")
    pos = 5 + nlen
    n = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    wide_off, wide_codes = bool(flags & 8), bool(flags & 4)
    osz = 4 if wide_off else 2
    fmt = "<I" if wide_off else "<H"
    offsets = [struct.unpack_from(fmt, data, pos + i * osz)[0] for i in range(n)]
    code_off = struct.unpack_from(fmt, data, pos + n * osz)[0]
    table_start = pos
    shapes = []
    for i in range(n):
        start = table_start + offsets[i]
        end = table_start + (offsets[i + 1] if i + 1 < n else code_off)
        shapes.append(glyph_contours(data[start:end]))
    cpos = table_start + code_off
    codes = []
    for i in range(n):
        if wide_codes:
            codes.append(struct.unpack_from("<H", data, cpos + 2 * i)[0])
        else:
            codes.append(data[cpos + i])
    cpos += (2 if wide_codes else 1) * n
    info = {"id": fid, "name": name, "bold": bool(flags & 1), "italic": bool(flags & 2)}
    adv = [EM // 2] * n
    if flags & 0x80:
        asc, desc, lead = struct.unpack_from("<hhh", data, cpos)
        cpos += 6
        adv = [struct.unpack_from("<h", data, cpos + 2 * i)[0] for i in range(n)]
        info.update(ascent=asc, descent=desc, leading=lead)
    return info, shapes, codes, adv


def build_ttf(info, shapes, codes, adv, path):
    names = [".notdef"] + ["g%d" % c for c in codes]
    pen = TTGlyphPen(None)
    glyphs = {".notdef": pen.glyph()}
    metrics = {".notdef": (EM // 2, 0)}
    for c, sh, a in zip(codes, shapes, adv):
        pen = TTGlyphPen(None)
        for contour in sh:
            for seg in contour:
                if seg[0] == "M":
                    pen.moveTo((seg[1] // SCALE, -seg[2] // SCALE))
                elif seg[0] == "L":
                    pen.lineTo((seg[1] // SCALE, -seg[2] // SCALE))
                else:
                    pen.qCurveTo((seg[1] // SCALE, -seg[2] // SCALE), (seg[3] // SCALE, -seg[4] // SCALE))
            pen.closePath()
        glyphs["g%d" % c] = pen.glyph()
        metrics["g%d" % c] = (max(a, 0) // SCALE, 0)
    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({c: "g%d" % c for c in codes})
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    asc, desc = info.get("ascent", EM * 16) // SCALE, info.get("descent", EM * 4) // SCALE
    fb.setupHorizontalHeader(ascent=asc, descent=-desc)
    fb.setupNameTable({"familyName": info["name"], "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-desc, usWinAscent=asc, usWinDescent=desc)
    fb.setupPost()
    fb.save(path)


def main(game_dir, out_dir):
    out = Path(out_dir) / "fonts"
    out.mkdir(parents=True, exist_ok=True)
    summary = {}
    for p in sorted(Path(game_dir).rglob("*.swf")):
        for t in read_swf(p)[3]:
            if t.code == 75:
                info, shapes, codes, adv = parse_font3(t.data)
                fname = "%s_%s" % (p.stem, info["name"].replace(" ", ""))
                build_ttf(info, shapes, codes, adv, out / (fname + ".ttf"))
                info["swf"] = p.stem
                info["glyphs"] = len(codes)
                info["has_accents"] = all(ord(c) in codes for c in "áéíóúñ¿¡")
                summary[fname] = info
                print("%-40s glyphs=%-4d accents=%s" % (fname, len(codes), info["has_accents"]))
    index = out / "fonts.json"
    if index.exists():  # several runs (data SWFs, main SWF) share one output directory
        try:
            summary = {**json.loads(index.read_text()), **summary}
        except ValueError:
            pass
    index.write_text(json.dumps(summary, indent=1))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
