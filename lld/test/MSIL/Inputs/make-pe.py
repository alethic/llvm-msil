"""Write a minimal PE32+ image, for lld/test/MSIL (msil-ld's input checks).

Usage: make-pe.py [--cli] [--dll] <out>

The image has the headers, one '.text' section and nothing else that runs:
it is only an input whose kind msil-ld must recognize (ImplementationPlan.md
5.9.4). With --cli, data directory 14 points at an ECMA-335 CLI header
(II.25.3.3, ILONLY) whose metadata directory points at a metadata root with
no streams: a PE with a CLI header, which msil-ld rejects before M20. Without
it the image is a native one. --dll sets IMAGE_FILE_DLL. The bytes depend only
on the options.
"""

import struct
import sys

FILE_ALIGN = 0x200
SECT_ALIGN = 0x1000
TEXT_RVA = 0x1000


def cli_section():
    # Metadata root (II.24.2.1) with zero streams, at offset 0x48.
    version = b"v4.0.30319\0\0"
    root = struct.pack("<IHHII", 0x424A5342, 1, 1, 0, len(version))
    root += version + struct.pack("<HH", 0, 0)
    # CLI header (II.25.3.3): cb, runtime 2.5, metadata, flags ILONLY,
    # entry point token 0, then six empty directories.
    header = struct.pack("<IHH", 72, 2, 5)
    header += struct.pack("<II", TEXT_RVA + 72, len(root))
    header += struct.pack("<II", 1, 0)
    header += b"\0" * (72 - len(header))
    return header + root


def main():
    args = sys.argv[1:]
    cli = "--cli" in args
    dll = "--dll" in args
    out = [a for a in args if not a.startswith("--")]
    if len(out) != 1:
        sys.exit("usage: make-pe.py [--cli] [--dll] <out>")

    text = cli_section() if cli else b"\xc3"  # ret
    raw_size = (len(text) + FILE_ALIGN - 1) // FILE_ALIGN * FILE_ALIGN
    text = text.ljust(raw_size, b"\0")

    dos = bytearray(0x80)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x80)

    characteristics = 0x0022 | (0x2000 if dll else 0)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, 240,
                                    characteristics)

    dirs = [(0, 0)] * 16
    if cli:
        dirs[14] = (TEXT_RVA, 72)
    size_of_image = TEXT_RVA + SECT_ALIGN
    opt = struct.pack("<HBBIIIII", 0x20B, 14, 0, raw_size, 0, 0,
                      0 if cli else TEXT_RVA, TEXT_RVA)
    opt += struct.pack("<QII", 0x180000000 if dll else 0x140000000,
                       SECT_ALIGN, FILE_ALIGN)
    opt += struct.pack("<HHHHHH", 6, 0, 0, 0, 6, 0)
    opt += struct.pack("<IIII", 0, size_of_image, FILE_ALIGN, 0)
    opt += struct.pack("<HH", 3, 0x8160)
    opt += struct.pack("<QQQQ", 0x100000, 0x1000, 0x100000, 0x1000)
    opt += struct.pack("<II", 0, 16)
    for rva, size in dirs:
        opt += struct.pack("<II", rva, size)
    assert len(opt) == 240

    section = struct.pack("<8sIIIIIIHHI", b".text", len(text), TEXT_RVA,
                          raw_size, FILE_ALIGN, 0, 0, 0, 0, 0x60000020)

    headers = bytes(dos) + coff + opt + section
    headers = headers.ljust(FILE_ALIGN, b"\0")
    with open(out[0], "wb") as f:
        f.write(headers + text)


if __name__ == "__main__":
    main()
