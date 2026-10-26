"""Decode the relocation table of a linked image, for lld/test/MSIL.

Usage: relocs.py <listing>

<listing> is llvm-cil-dump's canonical listing (ImplementationPlan.md
5.7.10). Above the relocation loop threshold the trailer of 5.9.10 keeps
its relocations in the RVA field '<msil>relocs' (type '<msil>relocs_t',
explicit layout, pack 1): 16-byte entries {u32 Site; u32 Kind; i64 Value},
little-endian. The listing prints the field's FieldRVA data as hex lines
under '.data <n> bytes'; this script prints one line per entry,

  <site> <kind> <value>

in table order, or 'no relocs' when the field is absent.
"""

import struct
import sys


def main():
    lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
    data, i = None, 0
    while i < len(lines):
        if lines[i].startswith("  .field '<msil>relocs' "):
            i += 1
            if i >= len(lines) or not lines[i].startswith("    .data "):
                sys.exit("relocs.py: '<msil>relocs' has no data of known size")
            data = bytearray()
            i += 1
            while i < len(lines) and lines[i].startswith("      "):
                data += bytes(int(b, 16) for b in lines[i].split())
                i += 1
            break
        i += 1
    if data is None:
        print("no relocs")
        return
    if len(data) % 16:
        sys.exit("relocs.py: %d bytes is not a whole number of entries" % len(data))
    for off in range(0, len(data), 16):
        site, kind, value = struct.unpack_from("<IIq", data, off)
        print("%d %d %d" % (site, kind, value))


if __name__ == "__main__":
    main()
