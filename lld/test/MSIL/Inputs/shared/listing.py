"""Print one part of a canonical listing, for lld/test/MSIL's shared-*.test files.

Usage: listing.py <mode> <listing> <class> [<method>]

<listing> is llvm-cil-dump's canonical listing (ImplementationPlan.md
5.7.10): per TypeDef an empty line, '.class '<full>' flags=...', then its
members indented by two blanks and their contents by four. Modes:

  class    the class line and every line of the class
  method   the '.method '<method>' ...' line of the class and the lines of
           its body (several methods of that name follow each other)
  members  the class line and the declaration lines of its members, without
           method bodies: '.field', '.property', '.get'/'.set'/'.other',
           '.method', '.param', '.pinvokeimpl' and '.custom' lines, so two
           classes whose members have the same names, signatures and flags
           print the same text whatever their bodies are

A class or method that the listing does not have prints 'no class <class>'
or 'no method <class>::<method>', so a FileCheck test sees the difference
between an empty part and a missing one.
"""

import sys


def quote(name):
    out = []
    for ch in name:
        if ch in "\\'":
            out.append("\\" + ch)
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%02X" % ord(ch))
        else:
            out.append(ch)
    return "'" + "".join(out) + "'"


def class_lines(lines, cls):
    head = ".class %s flags=" % quote(cls)
    for i, line in enumerate(lines):
        if line.startswith(head):
            j = i + 1
            while j < len(lines) and lines[j].startswith("  "):
                j += 1
            return lines[i:j]
    return None


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__.splitlines()[2])
    mode, path, cls = sys.argv[1:4]
    lines = open(path, encoding="utf-8").read().splitlines()
    body = class_lines(lines, cls)
    if body is None:
        print("no class " + cls)
        return
    if mode == "class":
        print("\n".join(body))
    elif mode == "members":
        keep = (".field", ".property", ".get", ".set", ".other", ".method", ".param",
                ".pinvokeimpl", ".custom")
        out = [body[0]]
        for line in body[1:]:
            text = line.lstrip(" ")
            depth = len(line) - len(text)
            if depth == 2 or (depth == 4 and text.startswith(keep)):
                out.append(line)
        print("\n".join(out))
    elif mode == "method":
        if len(sys.argv) != 5:
            sys.exit("listing.py: method needs a method name")
        head = "  .method %s " % quote(sys.argv[4])
        out = []
        inside = False
        for line in body[1:]:
            if line.startswith(head):
                inside = True
                out.append(line)
            elif inside and line.startswith("    "):
                out.append(line)
            else:
                inside = False
        print("\n".join(out) if out else "no method %s::%s" % (cls, sys.argv[4]))
    else:
        sys.exit("listing.py: unknown mode " + mode)


if __name__ == "__main__":
    main()
