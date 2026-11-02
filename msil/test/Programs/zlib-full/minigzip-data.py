#!/usr/bin/env python3
# ===-- minigzip-data.py - Data and paths of zlib-full's minigzip test ----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The input, the run directories and the comparisons of minigzip.test.

    minigzip-data.py setup [--wsl] --levels L[,L...] [--also TAG[,TAG...]] DIR
    minigzip-data.py cmp EXPECTED ACTUAL

setup writes DIR/input, a 2 MiB input that this script generates the same
way on every host and Python version (its own xorshift generator, no
vendored data; plan 5.17.15), and prints 'input: <size> bytes, sha256 <hex>'
so that the test pins it. For each level L it makes the run directories
DIR/L/<run>/ and the argument files DIR/L/<run>.arg:

    msil, native, netfx      compress runs ('minigzip FILE'): the directory
                             holds a copy of the input named 'data', and the
                             argument file names DIR/L/<run>/data
    msil-d, netfx-d          decompress runs ('minigzip -d FILE'): the test
                             copies a gzip stream into the directory as
                             'data.gz', and the argument file names it
    TAG, TAG-d               for each TAG of --also (also-net<major>[-tc0],
                             the msil_also_run entries that lit.local.cfg's
                             %{minigzip_also_tags} lists; '' for none): a
                             compress and a decompress run, as above

minigzip compresses FILE into FILE.gz and decompresses FILE.gz into FILE,
removing its input either way, so each run gets a directory of its own. An
argument file holds one absolute path, without a newline, for lit's
%{readfile:...}, in the form the program sees: the host's path, or with
--wsl (the test passes it under the feature msil-wsl, where msil-run.py runs
the MSIL programs and the E7h native oracle inside WSL and passes their
arguments unchanged) the /mnt/<drive>/... path of a Windows path, as
msil-run.py translates its own paths. The program runs from a private
directory (msil-run.py), so a relative path would name a file there, which
msil-run.py removes after the run; the streams have to reach the test, and
binary data never passes through a transcript.

cmp prints 'same: <n> bytes' when the two files are equal and exits 0, else
it prints both sizes and the offset of the first byte that differs and exits
1. Exit status 2: a usage error or a file that cannot be read or written.
"""

import argparse
import hashlib
import os
import re
import shutil
import sys

SIZE = 2 * 1024 * 1024
BLOCK = 64 * 1024
COMPRESS_RUNS = ("msil", "native", "netfx")
DECOMPRESS_RUNS = ("msil-d", "netfx-d")
ALSO_TAG = re.compile(r"^also-net[0-9]+(-tc0)?$")
LEVELS = ("O0", "O1", "O2", "O3", "Os", "Oz")
MASK64 = (1 << 64) - 1

WORDS = (
    "the of and to in is was for that with as on by at from this which or be "
    "are it an not have has had were but all their one been also its more "
    "than other into some time when only new first can would after two may "
    "deflate inflate window literal length distance huffman tree block "
    "stored fixed dynamic adler crc checksum header trailer stream buffer "
    "compress level strategy dictionary flush finish gzip zlib minigzip "
    "managed native assembly runtime variadic printf format"
).split()


class XorShift:
    """xorshift64* (Vigna): a fixed generator, so the input never depends
    on Python's random module."""

    def __init__(self, seed):
        self.x = seed & MASK64 or 1

    def next(self):
        x = self.x
        x ^= x >> 12
        x ^= (x << 25) & MASK64
        x ^= x >> 27
        self.x = x
        return (x * 0x2545F4914F6CDD1D) & MASK64

    def below(self, n):
        return (self.next() >> 32) % n


def text_block(rng, size):
    """Lines of words: a text that deflate's dynamic blocks compress well."""
    out = bytearray()
    while len(out) < size:
        line = [WORDS[rng.below(len(WORDS))] for _ in range(4 + rng.below(12))]
        if rng.below(8) == 0:
            line.append(str(rng.next() % 100000))
        out += (" ".join(line) + (".\n" if rng.below(3) == 0 else "\n")).encode()
    return bytes(out[:size])


def random_block(rng, size):
    """Bytes of every value with no structure: deflate stores them."""
    out = bytearray()
    while len(out) < size:
        out += rng.next().to_bytes(8, "little")
    return bytes(out[:size])


def small_alphabet_block(rng, size):
    """Four symbols: short codes, few long matches."""
    return bytes(b"ACGT"[rng.below(4)] for _ in range(size))


def repeat_block(rng, size):
    """Long runs and repeated phrases at distances up to 32 KiB."""
    out = bytearray()
    while len(out) < size:
        kind = rng.below(3)
        if kind == 0:
            out += bytes([rng.below(256)]) * (3 + rng.below(300))
        elif kind == 1 and len(out) > 300:
            dist = 1 + rng.below(min(len(out), 32768))
            length = 3 + rng.below(258)
            start = len(out) - dist
            for i in range(length):
                out.append(out[start + i])
        else:
            out += random_block(rng, 1 + rng.below(40))
    return bytes(out[:size])


def counter_block(rng, size):
    """Little-endian 32-bit counters with a random start and step."""
    out = bytearray()
    value, step = rng.next() & 0xFFFFFFFF, 1 + rng.below(1000)
    while len(out) < size:
        out += value.to_bytes(4, "little")
        value = (value + step) & 0xFFFFFFFF
    return bytes(out[:size])


KINDS = (
    text_block,
    text_block,
    random_block,
    text_block,
    small_alphabet_block,
    repeat_block,
    text_block,
    counter_block,
)


def generate():
    """The 2 MiB input: 32 blocks of 64 KiB, of the kinds above in turn."""
    rng = XorShift(0x9E3779B97F4A7C15)
    data = bytearray()
    for i in range(SIZE // BLOCK):
        data += KINDS[i % len(KINDS)](rng, BLOCK)
    assert len(data) == SIZE
    return bytes(data)


def wsl_path(path):
    """D:\\x\\y -> /mnt/d/x/y, as msil-run.py's to_wsl_path translates it."""
    full = os.path.realpath(os.path.abspath(path))
    drive, rest = os.path.splitdrive(full)
    if len(drive) != 2 or drive[1] != ":":
        raise ValueError("cannot translate %r into a WSL path" % full)
    return "/mnt/" + drive[0].lower() + rest.replace("\\", "/")


def write_arg(path, target, wsl):
    text = wsl_path(target) if wsl else os.path.abspath(target)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def setup(args):
    levels = [l for l in args.levels.split(",") if l]
    bad = [l for l in levels if l not in LEVELS]
    if not levels or bad or len(set(levels)) != len(levels):
        sys.stderr.write(
            "minigzip-data.py: --levels %r: give distinct levels of %s\n"
            % (args.levels, ",".join(LEVELS))
        )
        return 2
    also = [t for t in args.also.split(",") if t]
    bad = [t for t in also if not ALSO_TAG.match(t)]
    if bad or len(set(also)) != len(also):
        sys.stderr.write(
            "minigzip-data.py: --also %r: give distinct tags also-net<major>[-tc0]\n"
            % args.also
        )
        return 2
    compress = COMPRESS_RUNS + tuple(also)
    decompress = DECOMPRESS_RUNS + tuple(t + "-d" for t in also)
    data = generate()
    os.makedirs(args.dir, exist_ok=True)
    source = os.path.join(args.dir, "input")
    with open(source, "wb") as f:
        f.write(data)
    for level in levels:
        for run in compress + decompress:
            d = os.path.join(args.dir, level, run)
            if os.path.exists(d):
                shutil.rmtree(d)
            os.makedirs(d)
            if run in compress:
                target = os.path.join(d, "data")
                shutil.copyfile(source, target)
            else:
                target = os.path.join(d, "data.gz")
            write_arg(os.path.join(args.dir, level, run + ".arg"), target, args.wsl)
    print("input: %d bytes, sha256 %s" % (len(data), hashlib.sha256(data).hexdigest()))
    return 0


def cmp(args):
    with open(args.expected, "rb") as f:
        a = f.read()
    with open(args.actual, "rb") as f:
        b = f.read()
    if a == b:
        print("same: %d bytes" % len(a))
        return 0
    first = next(
        (i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b))
    )
    print(
        "differ: %s has %d bytes, %s has %d bytes; the first difference is at "
        "offset %d" % (args.expected, len(a), args.actual, len(b), first)
    )
    return 1


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("setup", help="write the input, run directories and arguments")
    s.add_argument("--wsl", action="store_true", help="write /mnt/<drive>/... paths")
    s.add_argument("--levels", required=True, help="L[,L...], for example O0,O2")
    s.add_argument(
        "--also", default="", help="TAG[,TAG...]: the msil_also_run runs (also-net10-tc0)"
    )
    s.add_argument("dir")
    c = sub.add_parser("cmp", help="compare two files byte for byte")
    c.add_argument("expected")
    c.add_argument("actual")
    args = p.parse_args(argv)
    try:
        return setup(args) if args.command == "setup" else cmp(args)
    except (OSError, ValueError) as e:
        sys.stderr.write("minigzip-data.py: %s\n" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
