#!/usr/bin/env python3
"""Prints the named display-list tree of a clip at a given frame (debugging aid).

Usage: dump_clip.py <extracted-dir> <swf-key> <class-name|id> [frame(1-based, default 1)] [depth]
"""
import json
import sys


def main():
    root, key, target = sys.argv[1:4]
    frame = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    maxd = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    c = json.load(open("%s/characters/%s.json" % (root, key.replace("/", "__"))))
    by_name = {v: int(k) for k, v in c["symbols"].items()}
    cid = by_name.get(target) if target in by_name else int(target)

    def state_at(frames, f):
        st = {}
        for i in range(min(f, len(frames))):
            for op in frames[i]["ops"]:
                if "remove" in op:
                    st.pop(op["remove"], None)
                elif "depth" in op:
                    if op.get("move") and op["depth"] in st:
                        st[op["depth"]].update({k: v for k, v in op.items() if k in ("id", "m", "name")})
                    elif "id" in op:
                        st[op["depth"]] = dict(op)
        return st

    def kind(i):
        s = str(i)
        for k in ("sprites", "shapes", "buttons", "texts", "statics"):
            if s in c.get(k, {}):
                return k[:-1]
        return "?"

    def walk(frames, f, indent, depth):
        for d, o in sorted(state_at(frames, f).items()):
            k = kind(o["id"])
            m = o.get("m", [1, 0, 0, 1, 0, 0])
            extra = ""
            if k == "sprite":
                extra = " frames=%d" % len(c["sprites"][str(o["id"])]["frames"])
            elif k == "text":
                t = c["texts"][str(o["id"])]
                extra = " text=%r font=%s size=%s" % (t.get("text"), t.get("font"), t.get("size"))
            print("%s[%d] %s#%d %s at (%.1f,%.1f)%s" % ("  " * indent, d, k, o["id"], ("name=" + o["name"]) if o.get("name") else "", m[4], m[5], extra))
            if k == "sprite" and indent < maxd:
                walk(c["sprites"][str(o["id"])]["frames"], 1, indent + 1, depth)

    sp = c["sprites"].get(str(cid))
    if sp is None:
        print("not a sprite:", kind(cid), cid)
        return
    labels = [(i + 1, f["label"]) for i, f in enumerate(sp["frames"]) if f.get("label")]
    print("sprite", cid, "frames", len(sp["frames"]), "labels", labels)
    walk(sp["frames"], frame, 0, maxd)


if __name__ == "__main__":
    main()
