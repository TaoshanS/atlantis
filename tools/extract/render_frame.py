#!/usr/bin/env python3
"""Debug renderer: draws one frame of a sprite (or the root timeline) from extractor output.

Usage: render_frame.py <extracted-dir> <swf-key> <sprite-id|root> <frame> <out.png> [WxH]
Only handles bitmap-filled and solid shapes (all this game needs). Child clips are
shown at the frame (frame % length), which is enough to validate placement.
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def mul(p, m):
    a, b, c, d, tx, ty = m
    pa, pb, pc, pd, ptx, pty = p
    return [pa * a + pc * b, pb * a + pd * b, pa * c + pc * d, pb * c + pd * d,
            pa * tx + pc * ty + ptx, pb * tx + pd * ty + pty]


def inv(m):
    a, b, c, d, tx, ty = m
    det = a * d - b * c
    if abs(det) < 1e-12:
        return None
    ia, ib, ic, id_ = d / det, -b / det, -c / det, a / det
    return [ia, ib, ic, id_, -(ia * tx + ic * ty), -(ib * tx + id_ * ty)]


class Renderer:
    def __init__(self, root, swf_key, size):
        self.root, self.key, self.size = Path(root), swf_key, size
        self.ch = json.loads((self.root / "characters" / (swf_key.replace("/", "__") + ".json")).read_text())
        self.cache = {}

    def bitmap(self, bid):
        if bid not in self.cache:
            p = self.root / "images" / self.key / ("%d.webp" % bid)
            self.cache[bid] = Image.open(p if p.exists() else p.with_suffix(".png")).convert("RGBA")
        return self.cache[bid]

    def draw_shape(self, canvas, sid, world):
        shp = self.ch["shapes"].get(str(sid))
        if not shp:
            return
        x0, x1, y0, y1 = shp["bounds"]
        corners = []
        for fx, fy in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            corners.append((world[0] * fx + world[2] * fy + world[4], world[1] * fx + world[3] * fy + world[5]))
        mask = Image.new("L", canvas.size, 0)
        ImageDraw.Draw(mask).polygon(corners, fill=255)
        for f in shp["fills"]:
            if f["t"] == "bitmap":
                if f["id"] == 0xFFFF or not any((self.root / "images" / self.key / ("%d.%s" % (f["id"], e))).exists() for e in ("webp", "png")):
                    continue  # 0xFFFF is the SWF "no bitmap" placeholder
                fm = f["m"]
                m = mul(world, [fm[0] / 20, fm[1] / 20, fm[2] / 20, fm[3] / 20, fm[4], fm[5]])
                iv = inv(m)
                if iv is None:
                    continue
                layer = self.bitmap(f["id"]).transform(canvas.size, Image.AFFINE,
                                                       (iv[0], iv[2], iv[4], iv[1], iv[3], iv[5]), Image.BICUBIC)
                a = layer.getchannel("A")
                from PIL import ImageChops
                layer.putalpha(ImageChops.multiply(a, mask))
                canvas.alpha_composite(layer)
            elif f["t"] == "solid":
                solid = Image.new("RGBA", canvas.size, tuple(f["c"]))
                solid.putalpha(mask.point(lambda v: v * f["c"][3] // 255))
                canvas.alpha_composite(solid)

    def draw_timeline(self, canvas, frames, frame, world, depth=0):
        if not frames or depth > 8:
            return
        display = {}
        for f in frames[: (frame % len(frames)) + 1]:
            for op in f["ops"]:
                if "remove" in op:
                    display.pop(op["remove"], None)
                elif "depth" in op:
                    cur = display.get(op["depth"], {}) if op.get("move") else {}
                    cur = dict(cur)
                    cur.update({k: v for k, v in op.items() if k in ("id", "m", "cx", "clip")})
                    display[op["depth"]] = cur
        from PIL import ImageChops
        clip_mask, clip_end = None, -1
        for d in sorted(display):
            o = display[d]
            if clip_mask is not None and d > clip_end:
                clip_mask = None
            if "id" not in o:
                continue
            w = mul(world, o.get("m", [1, 0, 0, 1, 0, 0]))
            target = canvas
            if "clip" in o:  # this object defines a mask for depths up to o["clip"]
                tmp = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
                self.draw_object(tmp, o, w, frame, depth)
                clip_mask, clip_end = tmp.getchannel("A"), o["clip"]
                continue
            if clip_mask is not None:
                target = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
            self.draw_object(target, o, w, frame, depth)
            if clip_mask is not None:
                target.putalpha(ImageChops.multiply(target.getchannel("A"), clip_mask))
                canvas.alpha_composite(target)

    def draw_object(self, canvas, o, w, frame, depth):
        cid = str(o["id"])
        if cid in self.ch["shapes"]:
            self.draw_shape(canvas, cid, w)
        elif cid in self.ch["sprites"]:
            self.draw_timeline(canvas, self.ch["sprites"][cid]["frames"], frame, w, depth + 1)


def main(root, key, target, frame, out, size="640x480"):
    w, h = map(int, size.split("x"))
    r = Renderer(root, key, (w, h))
    canvas = Image.new("RGBA", (w, h), (60, 60, 60, 255))
    frames = r.ch["root"] if target == "root" else r.ch["sprites"][target]["frames"]
    r.draw_timeline(canvas, frames, int(frame), [1, 0, 0, 1, w / 2, h / 2] if target != "root" else [1, 0, 0, 1, 0, 0])
    canvas.save(out)


if __name__ == "__main__":
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    main(*sys.argv[1:])
