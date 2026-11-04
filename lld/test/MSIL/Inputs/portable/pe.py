"""Rewrite or describe the PE envelope of a CIL object, for lld/test/MSIL's
portable-*.test files (ImplementationPlan.md 5.18.6 PL1, 5.18.7 PE1-PE3).

Usage:
  pe.py info <file>
      Print 'PE32 machine=0x014C' or 'PE32+ machine=0x8664' (the optional
      header magic and the COFF Machine), read from the headers alone.
  pe.py set-machine <in> <out> <machine>
      Copy <in> with only the COFF Machine field replaced (<machine> is a
      number, '0x8664' or '0x14c').
  pe.py to-pe32 <in> <out>
      Copy a PE32+ image as a PE32 image for machine I386 (0x14C): the
      optional header shrinks from 240 to 224 bytes (magic 0x10B,
      BaseOfData 0, a 32-bit ImageBase of 0x10000000, the four stack and heap
      fields as u32), the section table moves up by 16 bytes, and nothing
      after the headers moves. The CLI header's flags are kept (an
      msil-written object has ILONLY only).
  pe.py empty-archive <out>
      Write an archive with no members and no symbol index ('!<arch>\n'),
      the portable sysroot's libm.a of 5.18.6 PL2 (llvm-ar writes an empty
      symbol table member instead).
  pe.py rename-section <in> <out> <old> <new>
      Copy a CIL object with the section name <old> replaced by <new>, a
      name of the same length: the one occurrence of <old> in the file (the
      link record's segment name) is overwritten in place and nothing moves.
      It forges an object that llc would not write (a '.fini_array' segment,
      a null '.init_array' slot) so that msil-ld's own checks run; more or
      fewer than one occurrence, or a length change, is an error. Any other
      unique byte string of the file can be replaced the same way: the
      record's triple key (portable-import-module.test).

The rewrites follow the PE/COFF specification's field offsets (COFF header:
Machine at +0, SizeOfOptionalHeader at +16; optional header: PE32+ ImageBase
u64 at +24, stack and heap u64 at +72..+103, LoaderFlags at +104,
NumberOfRvaAndSizes at +108, data directories at +112; PE32 BaseOfData u32
at +24, ImageBase u32 at +28, stack and heap u32 at +72..+87, LoaderFlags at
+88, NumberOfRvaAndSizes at +92, data directories at +96), never from the
toolchain's writer.
"""

import struct
import sys

PE32_MAGIC = 0x10B
PE32PLUS_MAGIC = 0x20B


def headers(data):
    if data[:2] != b"MZ":
        sys.exit("not a PE image (no MZ)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        sys.exit("not a PE image (no PE signature)")
    coff = pe + 4
    opt = coff + 20
    return coff, opt


def info(path):
    data = open(path, "rb").read()
    coff, opt = headers(data)
    machine = struct.unpack_from("<H", data, coff)[0]
    magic = struct.unpack_from("<H", data, opt)[0]
    fmt = {PE32_MAGIC: "PE32", PE32PLUS_MAGIC: "PE32+"}.get(magic, "magic=0x%X" % magic)
    print("%s machine=0x%04X" % (fmt, machine))


def set_machine(src, dst, machine):
    data = bytearray(open(src, "rb").read())
    coff, _ = headers(data)
    struct.pack_into("<H", data, coff, int(machine, 0))
    open(dst, "wb").write(bytes(data))


def to_pe32(src, dst):
    data = bytearray(open(src, "rb").read())
    coff, opt = headers(data)
    nsections = struct.unpack_from("<H", data, coff + 2)[0]
    optsize = struct.unpack_from("<H", data, coff + 16)[0]
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != PE32PLUS_MAGIC or optsize != 240:
        sys.exit("to-pe32: input is not a PE32+ image with a 240-byte optional header")
    old = bytes(data[opt:opt + 240])
    sections = bytes(data[opt + 240:opt + 240 + 40 * nsections])

    new = bytearray(224)
    new[0:24] = old[0:24]  # Magic .. BaseOfCode
    struct.pack_into("<H", new, 0, PE32_MAGIC)
    struct.pack_into("<I", new, 24, 0)  # BaseOfData
    struct.pack_into("<I", new, 28, 0x10000000)  # ImageBase
    new[32:72] = old[32:72]  # SectionAlignment .. DllCharacteristics
    for i in range(4):  # stack reserve/commit, heap reserve/commit
        value = struct.unpack_from("<Q", old, 72 + 8 * i)[0]
        struct.pack_into("<I", new, 72 + 4 * i, value & 0xFFFFFFFF)
    new[88:96] = old[104:112]  # LoaderFlags, NumberOfRvaAndSizes
    new[96:224] = old[112:240]  # 16 data directories

    struct.pack_into("<H", data, coff, 0x14C)  # IMAGE_FILE_MACHINE_I386
    struct.pack_into("<H", data, coff + 16, 224)
    end = opt + 240 + 40 * nsections
    data[opt:end] = bytes(new) + sections + b"\0" * 16
    open(dst, "wb").write(bytes(data))


def empty_archive(dst):
    open(dst, "wb").write(b"!<arch>\n")


def rename_section(src, dst, old, new):
    old, new = old.encode("ascii"), new.encode("ascii")
    if len(old) != len(new):
        sys.exit("rename-section: '%s' and '%s' differ in length" % (old.decode(), new.decode()))
    data = open(src, "rb").read()
    count = data.count(old)
    if count != 1:
        sys.exit("rename-section: '%s' occurs %d times in %s, not once" % (old.decode(), count, src))
    open(dst, "wb").write(data.replace(old, new))


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "info":
        info(args[1])
    elif len(args) == 4 and args[0] == "set-machine":
        set_machine(args[1], args[2], args[3])
    elif len(args) == 3 and args[0] == "to-pe32":
        to_pe32(args[1], args[2])
    elif len(args) == 2 and args[0] == "empty-archive":
        empty_archive(args[1])
    elif len(args) == 5 and args[0] == "rename-section":
        rename_section(args[1], args[2], args[3], args[4])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
