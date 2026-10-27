"""The '#Strings' bound of lld/test/MSIL/shared-size.test.

Usage: size.py gen <n> <out.ll> <names.txt>
       size.py check <names.txt> <stats.txt>

gen writes an x86_64-pc-windows-msvc-msil module with <n> exported
functions whose names are 12 to 18 bytes long (deterministic: a running
number and a filler), one per line in <names.txt>.

check reads the '# heap #Strings size=<n>' line of llvm-cil-dump
--stats-only (ImplementationPlan.md 5.7.10) from <stats.txt> and compares
it with the bound of M22.e's shared-size case: the sum over every exported
function f of len('<msil>addr_' + f) + 1 + len('<msil>fn_' + f) + 1, plus
4,096 bytes for everything else. A writer that stores f, '<msil>addr_f'
and '<msil>fn_f' as three strings needs about len(f) + 1 more per function
(more than the 4,096 bytes for 1,000 functions of 12 to 18 bytes), so the
bound holds only when f shares the tail of a longer string (suffix sharing,
as Roslyn's writer does; recon p2/exports-multiasm.md 5.1). It prints
'strings <size> within <bound>' and exits 0, or 'strings <size> exceed
<bound>' and exits 1.
"""

import re
import sys


def names(n):
    out = []
    for i in range(n):
        length = 12 + i % 7
        stem = "exp%d_" % i
        out.append((stem + "abcdefghijklmnopqrstuvwxyz")[:length])
    return out


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "gen":
        fns = names(int(sys.argv[2]))
        with open(sys.argv[3], "w", newline="\n") as f:
            f.write('target triple = "x86_64-pc-windows-msvc-msil"\n')
            for i, name in enumerate(fns):
                f.write("\ndefine i32 @%s() {\n  ret i32 %d\n}\n" % (name, i))
        with open(sys.argv[4], "w", newline="\n") as f:
            f.write("".join(name + "\n" for name in fns))
    elif len(sys.argv) == 4 and sys.argv[1] == "check":
        fns = open(sys.argv[2]).read().split()
        stats = open(sys.argv[3]).read()
        m = re.search(r"^# heap #Strings size=(\d+)$", stats, re.M)
        if not m:
            sys.exit("size.py: no '# heap #Strings' line")
        size = int(m.group(1))
        bound = sum(len("<msil>addr_" + f) + 1 + len("<msil>fn_" + f) + 1 for f in fns) + 4096
        print("strings %d %s %d" % (size, "within" if size <= bound else "exceed", bound))
        sys.exit(0 if size <= bound else 1)
    else:
        sys.exit(__doc__.splitlines()[2])


if __name__ == "__main__":
    main()
