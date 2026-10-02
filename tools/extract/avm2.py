"""Minimal ABC (ActionScript 3 bytecode) reader: constant pool, classes, methods and bodies."""
import struct


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u30(self):
        v = shift = 0
        while True:
            b = self.u8()
            v |= (b & 0x7F) << shift
            if not b & 0x80:
                return v & 0xFFFFFFFF
            shift += 7
            if shift > 35:
                raise ValueError("bad u30")

    def s32(self):
        v = self.u30()
        return v - (1 << 32) if v & 0x80000000 else v

    def s24(self):
        v = self.d[self.p] | self.d[self.p + 1] << 8 | self.d[self.p + 2] << 16
        self.p += 3
        return v - (1 << 24) if v & 0x800000 else v

    def f64(self):
        v = struct.unpack_from("<d", self.d, self.p)[0]; self.p += 8; return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v


# opcode -> operand schema: 'u' u30, 'b' u8, 's' s24 offset, '' none; tuples for several
OPS = {
    0x01: ("bkpt", ""), 0x02: ("nop", ""), 0x03: ("throw", ""), 0x04: ("getsuper", "u"), 0x05: ("setsuper", "u"),
    0x06: ("dxns", "u"), 0x07: ("dxnslate", ""), 0x08: ("kill", "u"), 0x09: ("label", ""),
    0x0C: ("ifnlt", "s"), 0x0D: ("ifnle", "s"), 0x0E: ("ifngt", "s"), 0x0F: ("ifnge", "s"), 0x10: ("jump", "s"),
    0x11: ("iftrue", "s"), 0x12: ("iffalse", "s"), 0x13: ("ifeq", "s"), 0x14: ("ifne", "s"), 0x15: ("iflt", "s"),
    0x16: ("ifle", "s"), 0x17: ("ifgt", "s"), 0x18: ("ifge", "s"), 0x19: ("ifstricteq", "s"), 0x1A: ("ifstrictne", "s"),
    0x1B: ("lookupswitch", "L"), 0x1C: ("pushwith", ""), 0x1D: ("popscope", ""), 0x1E: ("nextname", ""),
    0x1F: ("hasnext", ""), 0x20: ("pushnull", ""), 0x21: ("pushundefined", ""), 0x23: ("nextvalue", ""),
    0x24: ("pushbyte", "b"), 0x25: ("pushshort", "u"), 0x26: ("pushtrue", ""), 0x27: ("pushfalse", ""),
    0x28: ("pushnan", ""), 0x29: ("pop", ""), 0x2A: ("dup", ""), 0x2B: ("swap", ""), 0x2C: ("pushstring", "u"),
    0x2D: ("pushint", "u"), 0x2E: ("pushuint", "u"), 0x2F: ("pushdouble", "u"), 0x30: ("pushscope", ""),
    0x31: ("pushnamespace", "u"), 0x32: ("hasnext2", "uu"), 0x40: ("newfunction", "u"), 0x41: ("call", "u"),
    0x42: ("construct", "u"), 0x43: ("callmethod", "uu"), 0x44: ("callstatic", "uu"), 0x45: ("callsuper", "uu"),
    0x46: ("callproperty", "uu"), 0x47: ("returnvoid", ""), 0x48: ("returnvalue", ""), 0x49: ("constructsuper", "u"),
    0x4A: ("constructprop", "uu"), 0x4C: ("callproplex", "uu"), 0x4E: ("callsupervoid", "uu"), 0x4F: ("callpropvoid", "uu"),
    0x55: ("newobject", "u"), 0x56: ("newarray", "u"), 0x57: ("newactivation", ""), 0x58: ("newclass", "u"),
    0x59: ("getdescendants", "u"), 0x5A: ("newcatch", "u"), 0x5D: ("findpropstrict", "u"), 0x5E: ("findproperty", "u"),
    0x5F: ("finddef", "u"), 0x60: ("getlex", "u"), 0x61: ("setproperty", "u"), 0x62: ("getlocal", "u"),
    0x63: ("setlocal", "u"), 0x64: ("getglobalscope", ""), 0x65: ("getscopeobject", "b"), 0x66: ("getproperty", "u"),
    0x68: ("initproperty", "u"), 0x6A: ("deleteproperty", "u"), 0x6C: ("getslot", "u"), 0x6D: ("setslot", "u"),
    0x6E: ("getglobalslot", "u"), 0x6F: ("setglobalslot", "u"), 0x70: ("convert_s", ""), 0x71: ("esc_xelem", ""),
    0x72: ("esc_xattr", ""), 0x73: ("convert_i", ""), 0x74: ("convert_u", ""), 0x75: ("convert_d", ""),
    0x76: ("convert_b", ""), 0x77: ("convert_o", ""), 0x78: ("checkfilter", ""), 0x80: ("coerce", "u"),
    0x82: ("coerce_a", ""), 0x85: ("coerce_s", ""), 0x86: ("astype", "u"), 0x87: ("astypelate", ""),
    0x90: ("negate", ""), 0x91: ("increment", ""), 0x92: ("inclocal", "u"), 0x93: ("decrement", ""),
    0x94: ("declocal", "u"), 0x95: ("typeof", ""), 0x96: ("not", ""), 0x97: ("bitnot", ""),
    0xA0: ("add", ""), 0xA1: ("subtract", ""), 0xA2: ("multiply", ""), 0xA3: ("divide", ""), 0xA4: ("modulo", ""),
    0xA5: ("lshift", ""), 0xA6: ("rshift", ""), 0xA7: ("urshift", ""), 0xA8: ("bitand", ""), 0xA9: ("bitor", ""),
    0xAA: ("bitxor", ""), 0xAB: ("equals", ""), 0xAC: ("strictequals", ""), 0xAD: ("lessthan", ""),
    0xAE: ("lessequals", ""), 0xAF: ("greaterthan", ""), 0xB0: ("greaterequals", ""), 0xB1: ("instanceof", ""),
    0xB2: ("istype", "u"), 0xB3: ("istypelate", ""), 0xB4: ("in", ""), 0xC0: ("increment_i", ""),
    0xC1: ("decrement_i", ""), 0xC2: ("inclocal_i", "u"), 0xC3: ("declocal_i", "u"), 0xC4: ("negate_i", ""),
    0xC5: ("add_i", ""), 0xC6: ("subtract_i", ""), 0xC7: ("multiply_i", ""),
    0xD0: ("getlocal0", ""), 0xD1: ("getlocal1", ""), 0xD2: ("getlocal2", ""), 0xD3: ("getlocal3", ""),
    0xD4: ("setlocal0", ""), 0xD5: ("setlocal1", ""), 0xD6: ("setlocal2", ""), 0xD7: ("setlocal3", ""),
    0xEF: ("debug", "bubb"), 0xF0: ("debugline", "u"), 0xF1: ("debugfile", "u"), 0xF2: ("bkptline", "u"), 0xF3: ("timestamp", ""),
}


def decode_code(code):
    """Returns a list of (offset, name, operands)."""
    r = Reader(code)
    out = []
    while r.p < len(code):
        off = r.p
        op = r.u8()
        if op not in OPS:
            raise ValueError("unknown opcode 0x%x at %d" % (op, off))
        name, schema = OPS[op]
        args = []
        if schema == "L":
            default = r.s24(); n = r.u30()
            args = [default] + [r.s24() for _ in range(n + 1)]
        else:
            for ch in schema:
                args.append({"u": r.u30, "b": r.u8, "s": r.s24}[ch]())
        out.append((off, name, args))
    return out


class Abc:
    def __init__(self, data):
        r = Reader(data)
        r.u16(); r.u16()
        self.ints = [0] + [r.s32() for _ in range(max(r.u30() - 1, 0))]
        n = r.u30(); self.uints = [0] + [r.u30() for _ in range(max(n - 1, 0))]
        n = r.u30(); self.doubles = [0.0] + [r.f64() for _ in range(max(n - 1, 0))]
        n = r.u30(); self.strings = [""] + [r.bytes(r.u30()).decode("utf-8", "replace") for _ in range(max(n - 1, 0))]
        n = r.u30(); self.namespaces = [(0, 0)] + [(r.u8(), r.u30()) for _ in range(max(n - 1, 0))]
        n = r.u30(); self.ns_sets = [[]]
        for _ in range(max(n - 1, 0)):
            self.ns_sets.append([r.u30() for _ in range(r.u30())])
        n = r.u30(); self.multinames = [None]
        for _ in range(max(n - 1, 0)):
            k = r.u8()
            if k in (0x07, 0x0D): self.multinames.append((k, r.u30(), r.u30()))
            elif k in (0x0F, 0x10): self.multinames.append((k, r.u30()))
            elif k in (0x11, 0x12): self.multinames.append((k,))
            elif k in (0x09, 0x0E): self.multinames.append((k, r.u30(), r.u30()))
            elif k in (0x1B, 0x1C): self.multinames.append((k, r.u30()))
            elif k == 0x1D:
                base = r.u30(); params = [r.u30() for _ in range(r.u30())]
                self.multinames.append((k, base, params))
            else: raise ValueError("multiname kind 0x%x" % k)
        self.methods = []
        for _ in range(r.u30()):
            pc = r.u30(); ret = r.u30(); ptypes = [r.u30() for _ in range(pc)]
            name = r.u30(); flags = r.u8()
            if flags & 0x08:
                for _ in range(r.u30()): r.u30(); r.u8()
            if flags & 0x80:
                for _ in range(pc): r.u30()
            self.methods.append({"name": name, "params": pc})
        for _ in range(r.u30()):
            r.u30()
            for _ in range(r.u30()): r.u30()
        def traits():
            out = []
            for _ in range(r.u30()):
                name = r.u30(); kind = r.u8(); t = kind & 0xF; attr = kind >> 4
                if t in (0, 6):
                    slot = r.u30(); typ = r.u30(); vi = r.u30()
                    if vi: r.u8()
                    out.append({"name": name, "kind": t})
                elif t == 4:
                    r.u30(); out.append({"name": name, "kind": t, "class": r.u30()})
                elif t == 5:
                    r.u30(); out.append({"name": name, "kind": t, "function": r.u30()})
                else:
                    r.u30(); out.append({"name": name, "kind": t, "method": r.u30()})
                if attr & 4:
                    for _ in range(r.u30()): r.u30()
            return out
        ninst = r.u30()
        self.instances = []
        for _ in range(ninst):
            name = r.u30(); sup = r.u30(); flags = r.u8()
            if flags & 8: r.u30()
            for _ in range(r.u30()): r.u30()
            iinit = r.u30()
            self.instances.append({"name": name, "super": sup, "iinit": iinit, "traits": traits()})
        self.classes = []
        for _ in range(ninst):
            cinit = r.u30(); self.classes.append({"cinit": cinit, "traits": traits()})
        self.scripts = []
        for _ in range(r.u30()):
            init = r.u30(); self.scripts.append({"init": init, "traits": traits()})
        self.bodies = {}
        for _ in range(r.u30()):
            m = r.u30(); r.u30(); r.u30(); r.u30(); r.u30()
            code = r.bytes(r.u30())
            for _ in range(r.u30()):
                r.u30(); r.u30(); r.u30(); r.u30(); r.u30()
            traits()
            self.bodies[m] = code

    def mname(self, idx):
        m = self.multinames[idx] if idx < len(self.multinames) else None
        if not m: return "*"
        if m[0] in (0x07, 0x0D): return self.strings[m[2]]
        if m[0] in (0x09, 0x0E): return self.strings[m[1]]
        if m[0] == 0x1D: return self.mname(m[1])
        return "?"

    def qname(self, idx):
        m = self.multinames[idx]
        if m and m[0] in (0x07, 0x0D):
            ns = self.namespaces[m[1]]
            pkg = self.strings[ns[1]] if ns[0] else ""
            return (pkg + "." if pkg else "") + self.strings[m[2]]
        return self.mname(idx)
