"""Give an executable other names, for lld/test/MSIL/dispatch.test.

Usage: make-links.py <exe> <dir> <name>...

Each <name> becomes <dir>/<name> (with the '.exe' suffix on Windows), a hard
link to <exe>, so the renamed lld copies cost no disk space. lld picks its
flavor from the program name, which on Windows LLVM takes from the module file
name, so a link name works where a batch or shell wrapper would not. Where a
hard link is impossible (POSIX, another file system) a symbolic link is made
instead; the program name is then the link's name too. Nothing is ever
copied.
"""

import os
import sys


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: make-links.py <exe> <dir> <name>...")
    exe, out, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    if not os.path.isfile(exe):
        sys.exit("make-links.py: no such executable: " + exe)
    suffix = ".exe" if os.name == "nt" else ""
    os.makedirs(out, exist_ok=True)
    for name in names:
        dst = os.path.join(out, name + suffix)
        if os.path.lexists(dst):
            os.remove(dst)
        try:
            os.link(exe, dst)
        except OSError as e:
            if os.name == "nt":
                sys.exit("make-links.py: cannot hard-link %s to %s: %s" % (exe, dst, e))
            os.symlink(os.path.abspath(exe), dst)


if __name__ == "__main__":
    main()
