"""Summarize what a module initializer stores, for lld/test/MSIL.

Usage: cctor.py <listing>

<listing> is llvm-cil-dump's canonical listing (ImplementationPlan.md
5.7.10) of an image that msil-ld or ilasm wrote. The script evaluates the
straight-line part of '<Module>::.cctor' (the shape of 5.9.10) on a small
symbolic stack and prints one line per effect, in IL order:

  alloc <n>                     AllocHGlobal(<n>), aligned into '<msil>data'
  cpblk data+<off> <n>          the RVA image copied to '<msil>data' + <off>
  initblk data+<off> <n>        <n> zero bytes at '<msil>data' + <off>
  field <name> = data+<off>     an address field stored from '<msil>data'
  field <name> = &<method>      a field stored from ldftn (a '<msil>fn_' field)
  field <name> = <value>        any other stored value
  store<w> data+<off> = <value> a relocation stored at '<msil>data' + <off>,
                                <w> = i (pointer width) or i4
  call <method>                 a call made by '.cctor' (constructors and
                                '<msil>init_dataimports')
  loop                          a branch: the relocation loop of 5.9.10, where
                                the summary stops

Values print as data+<off>, &<method>, <field>+<off> (a field read with an
addend) or a decimal constant. Offsets of 0 print as data+0. The tests
FileCheck these lines, so field order ties (two fields at one offset) and
instruction numbering do not matter. A missing '.cctor' prints
'no .cctor'.
"""

import re
import sys

OPERAND = re.compile(r"^    L\d+: (\S+)(?: (.*))?$")
MEMBER = re.compile(r"'<Module>'::'((?:[^'\\]|\\.)*)'$")


def member(text):
    m = MEMBER.search(text or "")
    return m.group(1) if m else (text or "?")


def show(v):
    kind, base, off = v
    if kind == "const":
        return str(off)
    if kind == "data":
        return "data+%d" % off
    if kind == "ftn":
        return "&" + base
    if kind == "field":
        return base if off == 0 else "%s%+d" % (base, off)
    return base


def main():
    lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
    body, inside = [], False
    for line in lines:
        if line.startswith("  .method '.cctor' "):
            inside = True
            continue
        if inside:
            if not line.startswith("    "):
                break
            body.append(line)
    if not inside:
        print("no .cctor")
        return
    out, stack = [], []

    def pop():
        return stack.pop() if stack else ("other", "?", 0)

    for line in body:
        m = OPERAND.match(line)
        if not m:
            continue
        op, arg = m.group(1), m.group(2)
        if op == "unaligned." or op == "volatile.":
            continue
        if op.startswith("ldc.i4"):
            if op == "ldc.i4.m1":
                n = -1
            elif op in ("ldc.i4", "ldc.i4.s"):
                n = int(arg)
            else:
                n = int(op.split(".")[2])
            stack.append(("const", None, n))
        elif op == "ldc.i8":
            stack.append(("const", None, int(arg)))
        elif op.startswith("conv."):
            pass
        elif op == "ldsfld":
            name = member(arg)
            if name == "<msil>data":
                stack.append(("data", None, 0))
            else:
                stack.append(("field", name, 0))
        elif op == "ldsflda":
            stack.append(("other", "&" + member(arg), 0))
        elif op == "ldftn":
            stack.append(("ftn", member(arg), 0))
        elif op == "add":
            b, a = pop(), pop()
            if b[0] == "const" and a[0] in ("data", "field"):
                stack.append((a[0], a[1], a[2] + b[2]))
            elif a[0] == "const" and b[0] in ("data", "field"):
                stack.append((b[0], b[1], b[2] + a[2]))
            elif a[0] == "const" and b[0] == "const":
                stack.append(("const", None, a[2] + b[2]))
            else:
                stack.append(("other", "(%s + %s)" % (show(a), show(b)), 0))
        elif op == "and":
            b, a = pop(), pop()
            stack.append(("other", "aligned", 0))
        elif op == "call":
            name = member(arg)
            if name.endswith("'AllocHGlobal'") or "AllocHGlobal" in (arg or ""):
                size = pop()
                out.append("alloc %s" % show(size))
                stack.append(("other", "alloc", 0))
            else:
                out.append("call " + name)
        elif op == "stsfld":
            v = pop()
            name = member(arg)
            if name == "<msil>data":
                continue
            out.append("field %s = %s" % (name, show(v)))
        elif op in ("stind.i", "stind.i4", "stind.i8"):
            v, addr = pop(), pop()
            out.append("store%s %s = %s" % (op[len("stind."):], show(addr), show(v)))
        elif op == "cpblk":
            n, src, dst = pop(), pop(), pop()
            out.append("cpblk %s %s" % (show(dst), show(n)))
        elif op == "initblk":
            n, val, dst = pop(), pop(), pop()
            out.append("initblk %s %s" % (show(dst), show(n)))
        elif op == "pop":
            pop()
        elif op == "ret":
            break
        elif op.startswith("b") or op in ("switch", "leave", "leave.s"):
            out.append("loop")
            break
        else:
            stack.append(("other", op, 0))
    print("\n".join(out))


if __name__ == "__main__":
    main()
