"""Recovers frame scripts (stop(), playSound(...), gotoAndPlay(...), ...) from DoABC tags.

AS3 timelines keep their per-frame code in `frameN` methods of the symbol classes; the class
constructor registers them with addFrameScript(frameIndex, this.frameN). The scripts in this
game are tiny, so a small abstract interpreter over the bytecode is enough.
"""
import struct

from avm2 import Abc, decode_code


def _simulate(abc, code):
    """Returns a list of {"call": name, "args": [...], "recv": "..."} for each call statement."""
    stack, calls = [], []
    for _off, name, a in code:
        if name == "getlocal0":
            stack.append(("this",))
        elif name in ("pushbyte",):
            stack.append(("int", a[0] - 256 if a[0] > 127 else a[0]))
        elif name == "pushshort":
            stack.append(("int", a[0] - 65536 if a[0] > 32767 else a[0]))
        elif name == "pushint":
            stack.append(("int", abc.ints[a[0]]))
        elif name == "pushuint":
            stack.append(("int", abc.uints[a[0]]))
        elif name == "pushdouble":
            stack.append(("num", abc.doubles[a[0]]))
        elif name == "pushstring":
            stack.append(("str", abc.strings[a[0]]))
        elif name == "pushtrue":
            stack.append(("bool", True))
        elif name == "pushfalse":
            stack.append(("bool", False))
        elif name == "pushnull":
            stack.append(("null",))
        elif name in ("findpropstrict", "findproperty"):
            stack.append(("scope", abc.mname(a[0])))
        elif name == "getlex":
            stack.append(("lex", abc.mname(a[0])))
        elif name == "getproperty":
            base = stack.pop() if stack else ("?",)
            stack.append(("prop", base, abc.mname(a[0])))
        elif name in ("callpropvoid", "callproperty"):
            argc = a[1]
            args = [stack.pop() for _ in range(argc)][::-1] if argc else []
            recv = stack.pop() if stack else ("?",)
            calls.append({"call": abc.mname(a[0]), "args": [_val(x) for x in args], "recv": _val(recv)})
            if name == "callproperty":
                stack.append(("result",))
        elif name == "constructprop":
            argc = a[1]
            args = [stack.pop() for _ in range(argc)][::-1] if argc else []
            stack.pop() if stack else None
            stack.append(("new", abc.mname(a[0]), [_val(x) for x in args]))
        elif name in ("pushscope", "returnvoid", "debugline", "debugfile", "label", "nop", "coerce", "coerce_a", "convert_i", "convert_d", "convert_s"):
            pass
        elif name == "pop":
            if stack: stack.pop()
        elif name == "setproperty":
            if stack: stack.pop()
            if stack: stack.pop()
            calls.append({"call": "=" + abc.mname(a[0]), "args": [], "recv": "?"})
        else:
            calls.append({"call": "?" + name, "args": [], "recv": "?"})
    return calls


def _val(v):
    t = v[0]
    if t in ("int", "num", "str", "bool"):
        return v[1]
    if t == "this":
        return "this"
    if t == "scope":
        return "this"
    if t == "lex":
        return "lex:" + v[1]
    if t == "prop":
        return "%s.%s" % (_val(v[1]), v[2])
    if t == "new":
        return {"new": v[1], "args": v[2]}
    return None


def _constructor_registrations(abc, iinit):
    """[(frame_index, method_name)] from addFrameScript(...) calls in a constructor."""
    pending, out = [], []
    code = decode_code(abc.bodies[iinit]) if iinit in abc.bodies else []
    i = 0
    while i < len(code):
        _o, name, a = code[i]
        if name in ("pushbyte", "pushshort", "pushint"):
            v = a[0] - 256 if (name == "pushbyte" and a[0] > 127) else a[0]
            pending.append(("int", v))
        elif name in ("getlex", "getproperty"):
            pending.append(("method", abc.mname(a[0])))
        elif name == "callpropvoid" and abc.mname(a[0]) == "addFrameScript":
            args = pending[-a[1]:] if a[1] else []
            for k in range(0, len(args) - 1, 2):
                if args[k][0] == "int" and args[k + 1][0] == "method":
                    out.append((args[k][1], args[k + 1][1]))
            pending = []
        elif name in ("callpropvoid", "constructsuper", "callproperty"):
            pending = []
        i += 1
    return out


def extract(doabc_payloads):
    """doabc_payloads: iterable of DoABC tag data. Returns {class_name: {frame_index: [calls]}}."""
    result = {}
    for data in doabc_payloads:
        i = 4
        while data[i] != 0:
            i += 1
        abc = Abc(data[i + 1:])
        for inst in abc.instances:
            cls = abc.qname(inst["name"])
            methods = {}
            for tr in inst["traits"]:
                if tr["kind"] == 1:
                    methods[abc.mname(tr["name"])] = tr["method"]
            regs = _constructor_registrations(abc, inst["iinit"])
            scripts = {}
            for frame, mname in regs:
                m = methods.get(mname)
                if m is None or m not in abc.bodies:
                    continue
                scripts.setdefault(frame, []).extend(_simulate(abc, decode_code(abc.bodies[m])))
            if scripts:
                result[cls] = scripts
    return result
