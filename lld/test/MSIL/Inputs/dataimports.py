"""Summarize the native data imports of a linked image, for lld/test/MSIL.

Usage: dataimports.py <listing>

<listing> is llvm-cil-dump's canonical listing (ImplementationPlan.md
5.7.10). 5.10.12's initializer '<msil>init_dataimports'() stores the address
of each native data import, '<msil>dataimport'(module, name), into its
address field. For each 'stsfld' in that method the script prints

  import <field> = <module>!<name>

from the first two strings loaded ('ldstr') since the previous store, in
IL order (the helper's arguments; later strings, such as the
EntryPointNotFoundException message on the null branch, are ignored), or
'no data imports' when the method is absent.
"""

import re
import sys

LDSTR = re.compile(r'^    L\d+: ldstr "((?:[^"\\]|\\.)*)"$')
STSFLD = re.compile(r"^    L\d+: stsfld .*'<Module>'::'((?:[^'\\]|\\.)*)'$")


def main():
    lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
    inside, found, strings = False, False, []
    for line in lines:
        if line.startswith("  .method '<msil>init_dataimports' "):
            inside = found = True
            continue
        if inside:
            if not line.startswith("    "):
                break
            m = LDSTR.match(line)
            if m:
                strings.append(m.group(1))
                continue
            m = STSFLD.match(line)
            if m:
                pair = strings[:2] if len(strings) >= 2 else ["?", "?"]
                print("import %s = %s!%s" % (m.group(1), pair[0], pair[1]))
                strings = []
    if not found:
        print("no data imports")


if __name__ == "__main__":
    main()
