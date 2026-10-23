"""Copy the first bytes of a file, for lld/test/MSIL's malformed inputs.

Usage: truncate.py <in> <out> <size>

<out> gets the first <size> bytes of <in>: a native library cut short keeps
the magic that classifies it (ImplementationPlan.md 5.10.8, identify_magic)
but no longer parses, so msil-ld must report it by name instead of crashing.
"""

import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: truncate.py <in> <out> <size>")
    src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
    with open(src, "rb") as f:
        data = f.read()
    if size >= len(data):
        sys.exit("truncate.py: %s has only %d bytes" % (src, len(data)))
    with open(dst, "wb") as f:
        f.write(data[:size])


if __name__ == "__main__":
    main()
