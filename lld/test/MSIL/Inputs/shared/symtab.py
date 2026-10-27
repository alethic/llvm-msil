"""Encode a library symbol table, for lld/test/MSIL's shared-*.test files.

Usage: symtab.py [options] [--format payload|blob|il]

The payload of a library's '<msil>SymbolTable' attribute, written from
ImplementationPlan.md 5.9.15 and M22.a item (4) with 5.8.8's primitives
(u8, uleb, str = uleb byte length + UTF-8), never from msil-ld's encoder:

  magic 4D 53 59 4D ("MSYM"), u8 major (1), u8 minor (0), u8 flags
  (0x01 INTERFACE), uleb toolchain ABI version (1), str triple (without its
  environment version), str C runtime identity, 4 x u8 data model (sizeof
  void *, long, wchar_t, long double), uleb n + n str export classes,
  uleb n + n data symbols (str name, uleb size, u8 log2 alignment, u8 flags:
  1 const, 2 thread-local, 4 weak), uleb n + n flagged functions (str name,
  u8 flags: 1 variadic, 2 weak, 4 ABI; with ABI the definition's 5.8.8
  SIGNATURES descriptor follows: uleb CallFlags (NO_PROTOTYPE 1, VARARG 2,
  SRET 4), uleb FixedParams if VARARG, uleb SretSize and u8 SretAlignLog2 if
  SRET, uleb byval count and per byval parameter uleb Param, uleb Size and
  u8 AlignLog2).

The descriptor is the SIGNATURES entry without its token: a library's table
names the function, not a row (this is the reading of 5.9.15 the tests use).

Options (repeatable where it makes sense):
  --major N --minor N --flags N --abi N   header fields (1, 0, 0, 1)
  --triple T --runtime C --dm P,L,W,D     (x86_64-pc-windows-msvc-msil,
                                           none, 8,4,2,8)
  --class NAME                            an export class
  --data NAME:SIZE:ALIGNLOG2:FLAGS        a data symbol
  --func NAME:FLAGS[:DESC]                a flagged function; DESC is a
                                          '+'-separated list of noproto,
                                          vararg=N, sret=SIZE/ALIGNLOG2 and
                                          byval=PARAM/SIZE/ALIGNLOG2
  --trailing HEX                          bytes appended after the payload
  --magic TEXT                            another magic (4 characters)
Formats: payload (hex bytes), blob (the custom-attribute value: prolog
01 00, the uint8[] length as u32, the payload, 00 00 named arguments; the
framing of encodeByteArrayAttribute, 5.7.4) and il (the blob as an ILAsm
bytearray in parentheses). Hex bytes are uppercase and separated by one
blank, as llvm-cil-dump prints them.
"""

import argparse
import struct
import sys


def uleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | 0x80 if n else b)
        if not n:
            return bytes(out)


def sstr(s):
    b = s.encode("utf-8")
    return uleb(len(b)) + b


def descriptor(text):
    flags, fixed, sret, byval = 0, None, None, []
    for item in text.split("+") if text else []:
        key, _, value = item.partition("=")
        if key == "noproto":
            flags |= 1
        elif key == "vararg":
            flags |= 2
            fixed = int(value)
        elif key == "sret":
            flags |= 4
            size, align = value.split("/")
            sret = (int(size), int(align))
        elif key == "byval":
            param, size, align = value.split("/")
            byval.append((int(param), int(size), int(align)))
        else:
            sys.exit("symtab.py: unknown descriptor item '%s'" % item)
    out = uleb(flags)
    if fixed is not None:
        out += uleb(fixed)
    if sret is not None:
        out += uleb(sret[0]) + bytes([sret[1]])
    out += uleb(len(byval))
    for param, size, align in byval:
        out += uleb(param) + uleb(size) + bytes([align])
    return out


def payload(a):
    magic = a.magic.encode("ascii")
    if len(magic) != 4:
        sys.exit("symtab.py: --magic needs 4 characters")
    t = magic + bytes([a.major, a.minor, a.flags]) + uleb(a.abi)
    t += sstr(a.triple) + sstr(a.runtime)
    t += bytes(int(x) for x in a.dm.split(","))
    t += uleb(len(a.cls)) + b"".join(sstr(c) for c in a.cls)
    t += uleb(len(a.data))
    for d in a.data:
        name, size, align, flags = d.rsplit(":", 3)
        t += sstr(name) + uleb(int(size)) + bytes([int(align), int(flags)])
    t += uleb(len(a.func))
    for f in a.func:
        name, flags, desc = (f.split(":", 2) + [""])[:3]
        t += sstr(name) + bytes([int(flags)])
        if int(flags) & 4:
            t += descriptor(desc)
    return t + bytes.fromhex(a.trailing)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--magic", default="MSYM")
    p.add_argument("--major", type=int, default=1)
    p.add_argument("--minor", type=int, default=0)
    p.add_argument("--flags", type=int, default=0)
    p.add_argument("--abi", type=int, default=1)
    p.add_argument("--triple", default="x86_64-pc-windows-msvc-msil")
    p.add_argument("--runtime", default="none")
    p.add_argument("--dm", default="8,4,2,8")
    p.add_argument("--class", dest="cls", action="append", default=[])
    p.add_argument("--data", action="append", default=[])
    p.add_argument("--func", action="append", default=[])
    p.add_argument("--trailing", default="")
    p.add_argument("--format", choices=["payload", "blob", "il"], default="payload")
    a = p.parse_args()
    t = payload(a)
    if a.format != "payload":
        t = b"\x01\x00" + struct.pack("<I", len(t)) + t + b"\x00\x00"
    text = " ".join("%02X" % b for b in t)
    print("( %s )" % text if a.format == "il" else text)


if __name__ == "__main__":
    main()
