"""Parsers for SWF character tags: shapes, sprites (timelines), buttons, text and fonts.

All coordinates are returned in pixels (twips / 20).
"""
import struct

from swf import Tag, cstring

SHAPE_TAGS = {2: 1, 22: 2, 32: 3, 83: 4}
PLACE2, PLACE3, REMOVE, REMOVE2, SHOW_FRAME = 26, 70, 5, 28, 1
FRAME_LABEL, DEFINE_SPRITE, START_SOUND, DO_ACTION = 43, 39, 15, 12


class Bits:
    def __init__(self, data, pos=0):
        self.d, self.pos, self.bit = data, pos, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            byte = self.d[self.pos]
            v = (v << 1) | ((byte >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit, self.pos = 0, self.pos + 1
        return v

    def sb(self, n):
        v = self.ub(n)
        return v - (1 << n) if n and v >> (n - 1) else v

    def fb(self, n):
        return self.sb(n) / 65536.0

    def align(self):
        if self.bit:
            self.bit, self.pos = 0, self.pos + 1

    def u8(self):
        self.align(); v = self.d[self.pos]; self.pos += 1; return v

    def u16(self):
        self.align(); v = struct.unpack_from("<H", self.d, self.pos)[0]; self.pos += 2; return v

    def u32(self):
        self.align(); v = struct.unpack_from("<I", self.d, self.pos)[0]; self.pos += 4; return v

    def rect(self):
        n = self.ub(5)
        v = [self.sb(n) / 20.0 for _ in range(4)]
        self.align()
        return v  # xmin, xmax, ymin, ymax

    def matrix(self):
        a = d = 1.0
        b = c = 0.0
        if self.ub(1):
            n = self.ub(5); a, d = self.fb(n), self.fb(n)
        if self.ub(1):
            n = self.ub(5); b, c = self.fb(n), self.fb(n)
        n = self.ub(5)
        tx, ty = self.sb(n) / 20.0, self.sb(n) / 20.0
        self.align()
        return [a, b, c, d, tx, ty]

    def cxform(self, alpha=True):
        has_add, has_mult = self.ub(1), self.ub(1)
        n = self.ub(4)
        cnt = 4 if alpha else 3
        mult = [self.sb(n) / 256.0 for _ in range(cnt)] if has_mult else [1.0] * cnt
        add = [self.sb(n) for _ in range(cnt)] if has_add else [0] * cnt
        if not alpha:
            mult.append(1.0); add.append(0)
        self.align()
        return mult + add

    def rgba(self, alpha):
        c = [self.u8() for _ in range(4 if alpha else 3)]
        return c if alpha else c + [255]


def _gradient(r, shape):
    r.align()
    r.ub(2); r.ub(2); n = r.ub(4)
    return [[r.u8(), r.rgba(shape >= 3)] for _ in range(n)]


def _fill_styles(r, shape):
    n = r.u8()
    if n == 0xFF and shape >= 2:
        n = r.u16()
    out = []
    for _ in range(n):
        t = r.u8()
        if t == 0:
            out.append({"t": "solid", "c": r.rgba(shape >= 3)})
        elif t in (0x10, 0x12, 0x13):
            m = r.matrix()
            g = _gradient(r, shape)
            if t == 0x13:
                r.u16()
            out.append({"t": "gradient", "m": m, "g": g, "kind": t})
        elif 0x40 <= t <= 0x43:
            bid = r.u16()
            m = r.matrix()
            out.append({"t": "bitmap", "id": bid, "m": m, "smooth": t in (0x40, 0x42), "repeat": t in (0x40, 0x41)})
        else:
            raise ValueError("fill style 0x%x" % t)
    return out


def _line_styles(r, shape):
    n = r.u8()
    if n == 0xFF:
        n = r.u16()
    out = []
    for _ in range(n):
        if shape == 4:
            w = r.u16() / 20.0
            b1, _b2 = r.u8(), r.u8()
            if (b1 >> 4 & 3) == 2:
                r.u16()
            if b1 >> 3 & 1:
                _fill_styles_one(r, shape)
                out.append({"w": w})
            else:
                out.append({"w": w, "c": r.rgba(True)})
        else:
            out.append({"w": r.u16() / 20.0, "c": r.rgba(shape >= 3)})
    return out


def _fill_styles_one(r, shape):
    # A single fill style inside LINESTYLE2.
    t = r.u8()
    if t == 0:
        r.rgba(True)
    elif t in (0x10, 0x12, 0x13):
        r.matrix(); _gradient(r, shape)
        if t == 0x13:
            r.u16()
    else:
        r.u16(); r.matrix()


def parse_shape(tag):
    shape = SHAPE_TAGS[tag.code]
    r = Bits(tag.data)
    sid = r.u16()
    bounds = r.rect()
    if shape == 4:
        r.rect(); r.u8()
    fills = _fill_styles(r, shape)
    lines = _line_styles(r, shape)
    nf, nl = r.ub(4), r.ub(4)
    # A StyleChangeRecord with new styles starts a fresh table whose indices restart at 1. All tables are kept in one list and
    # the records are re-based (fill_base / line_base) so every path points at the right style (previously only the last table survived).
    fill_base = line_base = 0
    paths = []  # [fill0, fill1, line, [segments]]
    cur = [0, 0, 0, []]
    x = y = 0.0
    while True:
        if r.ub(1):  # edge record
            straight = r.ub(1)
            n = r.ub(4) + 2
            if straight:
                if r.ub(1):
                    dx, dy = r.sb(n) / 20.0, r.sb(n) / 20.0
                elif r.ub(1):
                    dx, dy = 0.0, r.sb(n) / 20.0
                else:
                    dx, dy = r.sb(n) / 20.0, 0.0
                x += dx; y += dy
                cur[3].append(["L", x, y])
            else:
                cx, cy = x + r.sb(n) / 20.0, y + r.sb(n) / 20.0
                x, y = cx + r.sb(n) / 20.0, cy + r.sb(n) / 20.0
                cur[3].append(["Q", cx, cy, x, y])
            continue
        flags = r.ub(5)
        if flags == 0:
            break
        new_style = flags & 16
        line_s, fill1, fill0, move = flags & 8, flags & 4, flags & 2, flags & 1
        if cur[3]:
            paths.append(cur)
            cur = [cur[0], cur[1], cur[2], []]
        if move:
            n = r.ub(5)
            x, y = r.sb(n) / 20.0, r.sb(n) / 20.0
        if fill0:
            v = r.ub(nf)
            cur[0] = v + fill_base if v else 0
        if fill1:
            v = r.ub(nf)
            cur[1] = v + fill_base if v else 0
        if line_s:
            v = r.ub(nl)
            cur[2] = v + line_base if v else 0
        if new_style:
            r.align()
            new_fills, new_lines = _fill_styles(r, shape), _line_styles(r, shape)
            fill_base, line_base = len(fills), len(lines)
            fills = fills + new_fills
            lines = lines + new_lines
            nf, nl = r.ub(4), r.ub(4)
            cur[0] = cur[1] = cur[2] = 0
        cur[3] = [["M", x, y]]
    if cur[3]:
        paths.append(cur)
    return sid, {"bounds": bounds, "fills": fills, "lines": lines, "paths": paths}


def parse_place(tag):
    r = Bits(tag.data)
    f1 = r.u8()
    f2 = r.u8() if tag.code == PLACE3 else 0
    op = {"depth": r.u16()}
    if tag.code == PLACE3 and (f2 & 0x08 or (f2 & 0x10 and f1 & 2)):
        op["class"] = cstring(tag.data, r.pos)[0]
        r.pos = cstring(tag.data, r.pos)[1]
    if f1 & 2: op["id"] = r.u16()
    if f1 & 1: op["move"] = True
    if f1 & 4: op["m"] = r.matrix()
    if f1 & 8: op["cx"] = r.cxform()
    if f1 & 16: op["ratio"] = r.u16()
    if f1 & 32:
        op["name"], r.pos = cstring(tag.data, r.pos)
    if f1 & 64: op["clip"] = r.u16()
    if f2 & 1: op["filters"] = True  # not expected in this game
    if f2 & 2: op["blend"] = True
    return op


def parse_timeline(tags):
    frames, ops, label = [], [], None
    for t in tags:
        c = t.code
        if c in (PLACE2, PLACE3):
            ops.append(parse_place(t))
        elif c == REMOVE2:
            ops.append({"remove": struct.unpack_from("<H", t.data, 0)[0]})
        elif c == REMOVE:
            ops.append({"remove": struct.unpack_from("<H", t.data, 2)[0]})
        elif c == FRAME_LABEL:
            label = cstring(t.data, 0)[0]
        elif c == START_SOUND:
            ops.append({"sound": struct.unpack_from("<H", t.data, 0)[0]})
        elif c == SHOW_FRAME:
            f = {"ops": ops}
            if label:
                f["label"] = label
            frames.append(f)
            ops, label = [], None
    return frames


def parse_sprite(tag):
    sid, nframes = struct.unpack_from("<HH", tag.data, 0)
    pos, inner = 4, []
    while pos < len(tag.data):
        h = struct.unpack_from("<H", tag.data, pos)[0]
        pos += 2
        code, ln = h >> 6, h & 63
        if ln == 63:
            ln = struct.unpack_from("<I", tag.data, pos)[0]; pos += 4
        inner.append(Tag(code, tag.data[pos:pos + ln]))
        pos += ln
        if code == 0:
            break
    return sid, {"frames": parse_timeline(inner)}


def parse_button2(tag):
    r = Bits(tag.data)
    bid = r.u16(); r.u8()
    action_off = r.u16()
    recs = []
    while True:
        f = r.u8()
        if f == 0:
            break
        rec = {"id": r.u16(), "depth": r.u16(), "m": r.matrix(), "cx": r.cxform(True),
               "states": [n for n, bit in (("up", 1), ("over", 2), ("down", 4), ("hit", 8)) if f & bit]}
        if f & 0x10: raise ValueError("button filters")
        if f & 0x20: r.u8()
        recs.append(rec)
    return bid, {"records": recs}


def parse_edit_text(tag):
    r = Bits(tag.data)
    tid = r.u16(); bounds = r.rect()
    a, b = r.u8(), r.u8()
    out = {"bounds": bounds, "html": bool(b & 2), "multiline": bool(a & 0x20), "wrap": bool(a & 0x40),
           "readonly": bool(a & 8), "autosize": bool(b & 0x40), "border": bool(b & 8), "outlines": bool(b & 1)}
    if a & 1: out["font"] = r.u16()
    if b & 0x80:
        out["fontclass"], r.pos = cstring(tag.data, r.pos)
    if a & 1 or b & 0x80: out["size"] = r.u16() / 20.0
    if a & 4: out["color"] = r.rgba(True)
    if a & 2: out["maxlen"] = r.u16()
    if b & 0x20:
        out["align"] = r.u8(); r.u16(); r.u16(); r.u16(); r.u16()
    out["var"], r.pos = cstring(tag.data, r.pos)
    if a & 0x80:
        out["text"], r.pos = cstring(tag.data, r.pos)
    return tid, out


def parse_font3(tag):
    import fonts as _fonts
    info, _shapes, codes, adv = _fonts.parse_font3(tag.data)
    return info["id"], {"name": info["name"], "glyphs": len(codes), "bold": info["bold"], "italic": info["italic"], "codes": codes}


def _s16(v):
    return v - 65536 if v >= 32768 else v


def parse_static_text(tag):
    """DefineText: returns (id, {"bounds", "m", "runs": [{"font","size","color","x","y","glyphs","advances"}]})."""
    r = Bits(tag.data)
    tid = r.u16()
    bounds = r.rect()
    m = r.matrix()
    gbits, abits = r.u8(), r.u8()
    runs, cur = [], None
    font, size, color, x, y = None, 12.0, [0, 0, 0, 255], 0.0, 0.0
    while True:
        flags = r.u8()
        if flags == 0:
            break
        if flags & 0x80:
            if flags & 8: font = r.u16()
            if flags & 4: color = r.rgba(tag.code == 33)
            if flags & 1: x = _s16(r.u16()) / 20.0
            if flags & 2: y = _s16(r.u16()) / 20.0
            if flags & 8: size = r.u16() / 20.0
            cur = None
        else:
            n = flags & 0x7F
            run = {"font": font, "size": size, "color": color, "x": x, "y": y, "glyphs": [], "advances": []}
            for _ in range(n):
                run["glyphs"].append(r.ub(gbits))
                a = r.sb(abits) / 20.0
                run["advances"].append(a)
                x += a
            r.align()
            runs.append(run)
    return tid, {"bounds": bounds, "m": m, "runs": runs}
