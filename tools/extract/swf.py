"""Minimal SWF reader (FWS/CWS/ZWS-less) used by the asset extractor."""
import struct
import zlib


class Tag:
    __slots__ = ("code", "data")

    def __init__(self, code, data):
        self.code = code
        self.data = data


def read_swf(path):
    """Returns (version, frame_size_twips, frame_rate, frame_count, tags)."""
    raw = open(path, "rb").read()
    sig = raw[:3]
    version = raw[3]
    if sig == b"CWS":
        body = zlib.decompress(raw[8:])
    elif sig == b"FWS":
        body = raw[8:]
    else:
        raise ValueError("unsupported SWF signature %r in %s" % (sig, path))
    nbits = body[0] >> 3
    rect_len = (5 + 4 * nbits + 7) // 8
    pos = rect_len
    rate = struct.unpack_from("<H", body, pos)[0] / 256.0
    frames = struct.unpack_from("<H", body, pos + 2)[0]
    pos += 4
    tags = []
    while pos < len(body):
        h = struct.unpack_from("<H", body, pos)[0]
        pos += 2
        code, ln = h >> 6, h & 63
        if ln == 63:
            ln = struct.unpack_from("<I", body, pos)[0]
            pos += 4
        tags.append(Tag(code, body[pos:pos + ln]))
        pos += ln
        if code == 0:
            break
    return version, rate, frames, tags


def cstring(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end].decode("utf-8", "replace"), end + 1


def header_info(path):
    """Stage rect in pixels (xmin, xmax, ymin, ymax), frame rate and frame count."""
    raw = open(path, "rb").read()
    body = zlib.decompress(raw[8:]) if raw[:3] == b"CWS" else raw[8:]
    nbits = body[0] >> 3
    vals, bit = [], 5
    for _ in range(4):
        v = 0
        for _ in range(nbits):
            v = (v << 1) | ((body[bit >> 3] >> (7 - (bit & 7))) & 1)
            bit += 1
        if v >> (nbits - 1):
            v -= 1 << nbits
        vals.append(v / 20.0)
    pos = (5 + 4 * nbits + 7) // 8
    rate = struct.unpack_from("<H", body, pos)[0] / 256.0
    frames = struct.unpack_from("<H", body, pos + 2)[0]
    return {"rect": vals, "frame_rate": rate, "frames": frames}
