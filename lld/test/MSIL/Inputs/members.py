"""List the classes of a canonical listing and their members, for
lld/test/MSIL's spill-class tests.

Usage: members.py [--limit N] <listing>

<listing> is llvm-cil-dump's canonical listing (ImplementationPlan.md
5.7.10). The script prints, per class in listing order,

  class <name> flags=<flags> extends=<type or ->
  method <class>::<name> flags=<flags>        (one per method)
  field <class>::<name> flags=<flags>         (one per field)
  count <class> methods=<n> fields=<n>

and with --limit N a last line 'limit N ok' when no class has more than N
methods or N fields (5.9.12's per-type limits, lowered by
--msil-type-member-limit), else 'limit N exceeded by <class>' and exit
status 1.
"""

import re
import sys

CLASS = re.compile(r"^\.class '((?:[^'\\]|\\.)*)' flags=(0x[0-9A-F]+)(?: extends (.*))?$")
MEMBER = re.compile(r"^  \.(method|field) '((?:[^'\\]|\\.)*)' .* flags=(0x[0-9A-F]+)(?: impl=0x[0-9A-F]+)?$")


def main():
    args = sys.argv[1:]
    limit = None
    if args and args[0] == "--limit":
        limit = int(args[1])
        args = args[2:]
    lines = open(args[0], encoding="utf-8").read().splitlines()
    classes = []
    for line in lines:
        m = CLASS.match(line)
        if m:
            classes.append([m.group(1), m.group(2), m.group(3) or "-", [], []])
            print("class %s flags=%s extends=%s" % tuple(classes[-1][:3]))
            continue
        m = MEMBER.match(line)
        if m and classes:
            kind, name, flags = m.groups()
            classes[-1][3 if kind == "method" else 4].append(name)
            print("%s %s::%s flags=%s" % (kind, classes[-1][0], name, flags))
    bad = None
    for name, _, _, methods, fields in classes:
        print("count %s methods=%d fields=%d" % (name, len(methods), len(fields)))
        if limit is not None and (len(methods) > limit or len(fields) > limit):
            bad = bad or name
    if limit is not None:
        if bad:
            print("limit %d exceeded by %s" % (limit, bad))
            sys.exit(1)
        print("limit %d ok" % limit)


if __name__ == "__main__":
    main()
