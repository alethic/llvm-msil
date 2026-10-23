"""Corrupt the first short import member of an import library, for
lld/test/MSIL's malformed inputs.

Usage: patch-import.py <in> <out> size|nametype

The first archive member with an import object header (Sig1 0, Sig2 0xFFFF;
llvm/include/llvm/BinaryFormat/COFF.h, ImportHeader) gets a SizeOfData
(offset 12) 7 bytes past the member ('size'), or the name type 7 in bits 2-4
of its Type field (offset 18), which no IMPORT_NAME_* value has
('nametype'). The archive still classifies as an import library
(ImplementationPlan.md 5.10.8), so msil-ld must report the member by name,
never read past it or crash.
"""

import struct
import sys


def main():
    if len(sys.argv) != 4 or sys.argv[3] not in ("size", "nametype"):
        sys.exit("usage: patch-import.py <in> <out> size|nametype")
    src, dst, mode = sys.argv[1:4]
    with open(src, "rb") as f:
        data = bytearray(f.read())
    if data[:8] != b"!<arch>\n":
        sys.exit("patch-import.py: %s is not an archive" % src)
    off = 8
    while off + 60 <= len(data):
        size = int(data[off + 48 : off + 58].decode().strip())
        body = off + 60
        sig1, sig2 = struct.unpack_from("<HH", data, body)
        if sig1 == 0 and sig2 == 0xFFFF and size >= 20:
            if mode == "size":
                sod = struct.unpack_from("<I", data, body + 12)[0]
                struct.pack_into("<I", data, body + 12, sod + 7)
            else:
                ty = struct.unpack_from("<H", data, body + 18)[0]
                struct.pack_into("<H", data, body + 18, (ty & ~0x1C) | (7 << 2))
            with open(dst, "wb") as f:
                f.write(data)
            return
        off = body + size + (size & 1)
    sys.exit("patch-import.py: %s has no short import member" % src)


if __name__ == "__main__":
    main()
