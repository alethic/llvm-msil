#!/usr/bin/env python3
# ===-- pe-flags.py - Print the PE envelope of a CLI image ----------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Print the PE envelope of a CLI image, for ../x86-runner.test.

    pe-flags.py FILE

prints one line 'pe-flags: magic 0x<m> machine 0x<c> corflags 0x<f>': the
optional header's magic (0x10b for PE32, 0x20b for PE32+), the COFF machine
(0x14c for I386, 0x8664 for AMD64) and the Flags of the CLI header (COR20;
0x1 ILONLY, 0x2 32BITREQUIRED, 0x20000 32BITPREFERRED). A file that is no PE
image with a CLI header is an error 'pe-flags: FILE: <why>' with exit status
1.
"""

import struct
import sys


class Bad(Exception):
    pass


def u16(data, off):
    if off + 2 > len(data):
        raise Bad("truncated at 0x%x" % off)
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    if off + 4 > len(data):
        raise Bad("truncated at 0x%x" % off)
    return struct.unpack_from("<I", data, off)[0]


def envelope(data):
    if data[:2] != b"MZ":
        raise Bad("no MZ header")
    pe = u32(data, 0x3C)
    if data[pe : pe + 4] != b"PE\0\0":
        raise Bad("no PE signature at 0x%x" % pe)
    coff = pe + 4
    machine = u16(data, coff)
    nsections = u16(data, coff + 2)
    opt_size = u16(data, coff + 16)
    opt = coff + 20
    magic = u16(data, opt)
    if magic == 0x10B:
        dirs = opt + 96
    elif magic == 0x20B:
        dirs = opt + 112
    else:
        raise Bad("optional header magic 0x%x" % magic)
    # Data directory 14: the CLI header.
    clr_rva = u32(data, dirs + 14 * 8)
    if not clr_rva:
        raise Bad("no CLI header")
    sections = opt + opt_size
    for i in range(nsections):
        s = sections + 40 * i
        vsize, va = u32(data, s + 8), u32(data, s + 12)
        raw_size, raw = u32(data, s + 16), u32(data, s + 20)
        if va <= clr_rva < va + max(vsize, raw_size):
            cor20 = raw + clr_rva - va
            return magic, machine, u32(data, cor20 + 16)
    raise Bad("CLI header RVA 0x%x in no section" % clr_rva)


def main(argv):
    if len(argv) != 1:
        sys.stderr.write("usage: pe-flags.py FILE\n")
        return 2
    try:
        with open(argv[0], "rb") as f:
            data = f.read()
        magic, machine, flags = envelope(data)
    except (OSError, Bad) as e:
        sys.stderr.write("pe-flags: %s: %s\n" % (argv[0], e))
        return 1
    # LF line endings on every host, for harness-check.py's equals.
    sys.stdout.buffer.write(
        b"pe-flags: magic 0x%x machine 0x%x corflags 0x%x\n" % (magic, machine, flags)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
