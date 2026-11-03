#!/usr/bin/env python3
"""Generate or check lld/MSIL/CLibraryNames.def (plan 5.18 PL3).

CLibraryNames.def lists the C library function names that TargetLibraryInfo
lacks (llvm/docs/MSIL/ImplementationPlan.md 5.18.6 PL3): the `name` of every
`functions:` entry of llvm-libc's public headers (libc/include/**/*.yaml),
less every quoted name in llvm/include/llvm/Analysis/TargetLibraryInfo.td.
The list is deduplicated and sorted by byte value, the order that
isCLibraryFunctionName (lld/MSIL/SymbolTable.cpp) asserts with
std::is_sorted and then binary-searches.

Usage:
  python msil/utils/gen-clibrary-names.py [--src-root REPO] [--def PATH]
  python msil/utils/gen-clibrary-names.py --check [--src-root REPO] [--def PATH]

With no mode the script rewrites only the block of C_LIBRARY_NAME(...) lines
of the .def, keeping the header and footer byte-for-byte. With --check it
compares the .def's entries with the regenerated list without writing: it
exits 0 when they are equal, or exits 1 printing 'CLibraryNames.def is
stale' and the missing and extra names. Standard library only.
"""

import argparse
import os
import re
import sys

ENTRY_RE = re.compile(r"^C_LIBRARY_NAME\(([^)]*)\)$")
# A top-level YAML key: `functions:`, `objects:`, ...
TOP_KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:")
# A sequence item that starts with `name:`, e.g. `  - name: strlen`.
ITEM_NAME_RE = re.compile(r"^(\s*)-\s+name\s*:\s*(.*?)\s*$")
ITEM_RE = re.compile(r"^(\s*)-\s")
# TargetLibraryInfo.td's names: the first argument of each TargetLibCall<...>.
TLI_NAME_RE = re.compile(r'TargetLibCall\s*<\s*"([^"]*)"')


def default_src_root():
    return os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def unquote(value):
    """Strips a trailing comment and one level of YAML quotes."""
    if value[:1] in ("'", '"'):
        quote = value[0]
        end = value.find(quote, 1)
        if end > 0:
            return value[1:end]
    return value.split(" #", 1)[0].strip()


def libc_function_names(src_root):
    """The `name` of every `functions:` entry of libc/include/**/*.yaml."""
    names = set()
    include_dir = os.path.join(src_root, "libc", "include")
    if not os.path.isdir(include_dir):
        sys.exit("error: no directory %s" % include_dir)
    for dirpath, dirnames, filenames in os.walk(include_dir):
        dirnames.sort()
        for filename in sorted(filenames):
            if not filename.endswith(".yaml"):
                continue
            path = os.path.join(dirpath, filename)
            with open(path, encoding="utf-8") as f:
                lines = f.read().splitlines()
            in_functions = False
            item_indent = None
            for line in lines:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                top = TOP_KEY_RE.match(line)
                if top:
                    in_functions = top.group(1) == "functions"
                    item_indent = None
                    continue
                if not in_functions:
                    continue
                item = ITEM_RE.match(line)
                if not item:
                    continue
                indent = len(item.group(1))
                # Only the entries of the `functions:` sequence itself, not
                # nested sequences such as `arguments:`.
                if item_indent is None:
                    item_indent = indent
                if indent != item_indent:
                    continue
                m = ITEM_NAME_RE.match(line)
                if m:
                    name = unquote(m.group(2))
                    if name:
                        names.add(name)
    return names


def tli_names(src_root):
    """The quoted names of TargetLibraryInfo.td's TargetLibCall records."""
    path = os.path.join(src_root, "llvm", "include", "llvm", "Analysis",
                        "TargetLibraryInfo.td")
    if not os.path.isfile(path):
        sys.exit("error: no file %s" % path)
    with open(path, encoding="utf-8") as f:
        return set(TLI_NAME_RE.findall(f.read()))


def generate(src_root):
    names = libc_function_names(src_root) - tli_names(src_root)
    # Byte-value order, as StringRef's operator< compares.
    return sorted(names, key=lambda n: n.encode("utf-8"))


def split_def(text):
    """Splits the .def into (header, entry names, footer).

    The entry block is the run of lines from the first C_LIBRARY_NAME(...)
    line to the last one; it may hold only entries and blank lines.
    """
    lines = text.splitlines(keepends=True)
    entry_lines = [i for i, line in enumerate(lines)
                   if ENTRY_RE.match(line.rstrip("\r\n"))]
    if not entry_lines:
        sys.exit("error: the .def has no C_LIBRARY_NAME(...) entries")
    first, last = entry_lines[0], entry_lines[-1]
    names = []
    for i in range(first, last + 1):
        body = lines[i].rstrip("\r\n")
        m = ENTRY_RE.match(body)
        if m:
            names.append(m.group(1))
        elif body.strip():
            sys.exit("error: line %d of the .def's entry block is not a "
                     "C_LIBRARY_NAME(...) entry: %s" % (i + 1, body))
    newline = "\r\n" if lines[first].endswith("\r\n") else "\n"
    return "".join(lines[:first]), names, "".join(lines[last + 1:]), newline


def main():
    parser = argparse.ArgumentParser(
        description="Generate or check lld/MSIL/CLibraryNames.def "
                    "(plan 5.18 PL3).")
    parser.add_argument("--src-root", default=default_src_root(),
                        help="the llvm-project checkout (default: the "
                             "repository containing this script)")
    parser.add_argument("--def", dest="def_path", default=None,
                        help="the .def to rewrite or check (default: "
                             "lld/MSIL/CLibraryNames.def under --src-root)")
    parser.add_argument("--check", action="store_true",
                        help="report whether the .def is current; do not "
                             "write it")
    args = parser.parse_args()

    src_root = os.path.abspath(args.src_root)
    def_path = args.def_path or os.path.join(src_root, "lld", "MSIL",
                                             "CLibraryNames.def")
    with open(def_path, "rb") as f:
        text = f.read().decode("utf-8")
    header, current, footer, newline = split_def(text)
    expected = generate(src_root)

    if args.check:
        if current == expected:
            print("CLibraryNames.def is current (%d names)" % len(expected))
            return 0
        current_set, expected_set = set(current), set(expected)
        missing = [n for n in expected if n not in current_set]
        extra = sorted(current_set - expected_set,
                       key=lambda n: n.encode("utf-8"))
        print("CLibraryNames.def is stale: %d missing, %d extra"
              % (len(missing), len(extra)))
        for name in missing:
            print("missing: %s" % name)
        for name in extra:
            print("extra: %s" % name)
        if not missing and not extra:
            print("the entries are not sorted or not unique")
        print("regenerate with: python msil/utils/gen-clibrary-names.py")
        return 1

    body = "".join("C_LIBRARY_NAME(%s)%s" % (n, newline) for n in expected)
    new_text = header + body + footer
    if new_text != text:
        with open(def_path, "wb") as f:
            f.write(new_text.encode("utf-8"))
        print("wrote %s (%d names)" % (def_path, len(expected)))
    else:
        print("%s is current (%d names)" % (def_path, len(expected)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
