"""Builds the proposed '<msil>SymbolTable' blob (format 1, by name) for E7 and prints it as an ILAsm bytearray.
usage: symtab.py <triple> <export-class> [data:<name>:<size>:<align> ...] [varfn:<name>]"""
import struct, sys
def cu(n):
    if n < 0x80: return bytes([n])
    if n < 0x4000: return bytes([0x80 | (n >> 8), n & 0xFF])
    return bytes([0xC0 | (n >> 24), (n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF])
def sstr(s): b = s.encode(); return cu(len(b)) + b
triple, cls = sys.argv[1], sys.argv[2]
data = [a.split(":")[1:] for a in sys.argv[3:] if a.startswith("data:")]
varfn = [a.split(":")[1] for a in sys.argv[3:] if a.startswith("varfn:")]
t = b"MSYM" + bytes([1, 0]) + struct.pack("<H", 1) + sstr(triple)
t += cu(1) + sstr(cls)
t += cu(len(data))
for n, sz, al in data:
    t += sstr(n) + cu(int(sz)) + bytes([int(al).bit_length() - 1, 0])
t += cu(len(varfn))
for n in varfn:
    t += sstr(n) + bytes([1])          # flags: 1 = variadic
blob = b"\x01\x00" + struct.pack("<I", len(t)) + t + b"\x00\x00"
print("( " + " ".join(f"{x:02X}" for x in blob) + " )")
