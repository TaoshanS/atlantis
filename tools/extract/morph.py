"""DefineMorphShape (tag 46): parsing and interpolation.

The game's three morph shapes (card dialogue outro mask, two chest shapes) are placed with a `ratio`. Instead of teaching the engine about morphs,
the extractor turns every (shape, ratio) pair that a timeline uses into an ordinary shape (`apply_to_characters`) and rewrites the placement.
"""
from characters import Bits

MORPH_SHAPE = 46


def _fills(r):
    n = r.u8()
    if n == 0xFF:
        n = r.u16()
    out = []
    for _ in range(n):
        t = r.u8()
        if t == 0:
            out.append({"t": "solid", "c0": r.rgba(True), "c1": r.rgba(True)})
        elif t in (0x10, 0x12):
            m0, m1 = r.matrix(), r.matrix()
            r.align()
            cnt = r.u8()
            stops = [(r.u8(), r.rgba(True), r.u8(), r.rgba(True)) for _ in range(cnt)]
            out.append({"t": "gradient", "m0": m0, "m1": m1, "stops": stops, "kind": t})
        elif 0x40 <= t <= 0x43:
            bid = r.u16()
            out.append({"t": "bitmap", "id": bid, "m0": r.matrix(), "m1": r.matrix(), "smooth": t in (0x40, 0x42), "repeat": t in (0x40, 0x41)})
        else:
            raise ValueError("morph fill style 0x%x" % t)
    return out


def _lines(r):
    n = r.u8()
    if n == 0xFF:
        n = r.u16()
    return [{"w0": r.u16() / 20.0, "w1": r.u16() / 20.0, "c0": r.rgba(True), "c1": r.rgba(True)} for _ in range(n)]


def _records(r):
    """Edge records of one SHAPE (no new styles): ('style', move, fill0, fill1, line) | ('L', dx, dy) | ('Q', cdx, cdy, adx, ady)."""
    nf, nl = r.ub(4), r.ub(4)
    recs = []
    while True:
        if r.ub(1):
            straight = r.ub(1)
            n = r.ub(4) + 2
            if straight:
                if r.ub(1):
                    dx, dy = r.sb(n) / 20.0, r.sb(n) / 20.0
                elif r.ub(1):
                    dx, dy = 0.0, r.sb(n) / 20.0
                else:
                    dx, dy = r.sb(n) / 20.0, 0.0
                recs.append(("L", dx, dy))
            else:
                recs.append(("Q", r.sb(n) / 20.0, r.sb(n) / 20.0, r.sb(n) / 20.0, r.sb(n) / 20.0))
            continue
        flags = r.ub(5)
        if flags == 0:
            break
        move = fill0 = fill1 = line = None
        if flags & 1:
            n = r.ub(5)
            move = (r.sb(n) / 20.0, r.sb(n) / 20.0)
        if flags & 2: fill0 = r.ub(nf)
        if flags & 4: fill1 = r.ub(nf)
        if flags & 8: line = r.ub(nl)
        if flags & 16:
            raise ValueError("new styles inside a morph shape")
        recs.append(("style", move, fill0, fill1, line))
    return recs


def parse_morph(tag):
    r = Bits(tag.data)
    cid = r.u16()
    b0, b1 = r.rect(), r.rect()
    r.u32()  # offset to the end edges
    fills, lines = _fills(r), _lines(r)
    r.align()
    start = _records(r)
    r.align()
    end = _records(r)
    return cid, {"bounds0": b0, "bounds1": b1, "fills": fills, "lines": lines, "start": start, "end": end}


def _lerp(a, b, t):
    return a + (b - a) * t


def _lerp_list(a, b, t):
    return [_lerp(x, y, t) for x, y in zip(a, b)]


def interpolate(m, t):
    """Shape dict (same layout as characters.parse_shape) of the morph at t in [0, 1]."""
    fills = []
    for f in m["fills"]:
        if f["t"] == "solid":
            fills.append({"t": "solid", "c": [int(round(v)) for v in _lerp_list(f["c0"], f["c1"], t)]})
        elif f["t"] == "bitmap":
            fills.append({"t": "bitmap", "id": f["id"], "m": _lerp_list(f["m0"], f["m1"], t), "smooth": f["smooth"], "repeat": f["repeat"]})
        else:
            s0 = f["stops"]
            g = [[int(round(_lerp(a, c, t))), [int(round(v)) for v in _lerp_list(ca, cb, t)]] for a, ca, c, cb in s0]
            fills.append({"t": "gradient", "m": _lerp_list(f["m0"], f["m1"], t), "g": g, "kind": f["kind"]})
    lines = [{"w": _lerp(l["w0"], l["w1"], t), "c": [int(round(v)) for v in _lerp_list(l["c0"], l["c1"], t)]} for l in m["lines"]]
    paths = []  # [fill0, fill1, line, [segments]]
    cur = [0, 0, 0, []]
    x = y = 0.0
    for a, b in zip(m["start"], m["end"]):
        if a[0] == "style":
            if cur[3]:
                paths.append(cur)
                cur = [cur[0], cur[1], cur[2], []]
            if a[1] is not None and b[1] is not None:
                x, y = _lerp(a[1][0], b[1][0], t), _lerp(a[1][1], b[1][1], t)
            if a[2] is not None: cur[0] = a[2]
            if a[3] is not None: cur[1] = a[3]
            if a[4] is not None: cur[2] = a[4]
            cur[3] = [["M", x, y]]
            continue
        if b[0] == "style":
            continue  # records out of step: skip (does not happen in this game's files)
        def as_q(rec):
            if rec[0] == "Q":
                return rec[1:]
            return (rec[1] / 2, rec[2] / 2, rec[1] / 2, rec[2] / 2)
        if a[0] == "L" and b[0] == "L":
            x += _lerp(a[1], b[1], t); y += _lerp(a[2], b[2], t)
            cur[3].append(["L", x, y])
        else:
            qa, qb = as_q(a), as_q(b)
            cx, cy = x + _lerp(qa[0], qb[0], t), y + _lerp(qa[1], qb[1], t)
            x, y = cx + _lerp(qa[2], qb[2], t), cy + _lerp(qa[3], qb[3], t)
            cur[3].append(["Q", cx, cy, x, y])
    if cur[3]:
        paths.append(cur)
    return {"bounds": _lerp_list(m["bounds0"], m["bounds1"], t), "fills": fills, "lines": lines, "paths": paths}


def apply_to_characters(chars, morphs):
    """Replaces `ratio` placements of morph shapes by plain shapes added to chars['shapes'] (ids start above every existing id)."""
    if not morphs:
        return
    next_id = max([0] + list(chars["shapes"]) + list(chars["sprites"]) + list(chars["buttons"]) + list(chars["texts"]) + list(chars["statics"]) + list(morphs)) + 1
    cache = {}

    def synth(mid, ratio):
        nonlocal next_id
        key = (mid, ratio)
        if key not in cache:
            chars["shapes"][next_id] = interpolate(morphs[mid], ratio / 65535.0)
            cache[key] = next_id
            next_id += 1
        return cache[key]

    def fix(frames):
        depth_char, depth_ratio = {}, {}
        for f in frames:
            for op in f["ops"]:
                if "remove" in op:
                    depth_char.pop(op["remove"], None)
                    continue
                if "depth" not in op:
                    continue
                d = op["depth"]
                cid = op.get("id", depth_char.get(d))
                if cid in morphs:
                    ratio = op.pop("ratio", depth_ratio.get(d, 0))  # a later `move` without ratio keeps the previous one
                    op["id"] = synth(cid, ratio)
                    depth_ratio[d] = ratio
                    depth_char[d] = cid  # keep the original morph id: later `move` ops carry only the ratio
                elif "id" in op:
                    depth_char[d] = op["id"]
    for sp in chars["sprites"].values():
        fix(sp["frames"])
    fix(chars["root"])
