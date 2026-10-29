"""Lists the variadic functions a C library declares and exports, and their
v-variants (the exported function that takes a va_list instead of ...).

usage: scan.py glibc <sysroot> <llvm-nm> <libc.so.6>
       scan.py ucrt <ucrt include dir> <ucrtbase exports file>
"""
import os
import re
import subprocess
import sys

DECL = re.compile(r'\b([A-Za-z_]\w*)\s*\(([^;{}()]|\([^()]*\))*?\.\.\.\s*\)', re.S)
SKIP = {'if', 'while', 'for', 'switch', 'return', 'sizeof', '__attribute__',
        '__typeof__', 'typeof', '__asm__', '__REDIRECT', '__REDIRECT_NTH',
        '__nonnull', '_Pragma', '__declspec'}


def scan(root):
    names = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith('.h'):
                continue
            p = os.path.join(d, f)
            try:
                s = open(p, encoding='latin-1').read()
            except OSError:
                continue
            s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
            s = re.sub(r'//[^\n]*', ' ', s)
            s = re.sub(r'^\s*#[^\n]*(\\\n[^\n]*)*', ' ', s, flags=re.M)
            for m in DECL.finditer(s):
                n = m.group(1)
                if n in SKIP:
                    continue
                names.setdefault(n, os.path.relpath(p, root).replace(os.sep, '/'))
    return names


def vname_candidates(n):
    c = ['v' + n]
    for pre in ('__isoc99_', '__isoc23_', '__', '_', 'obstack_', 'w'):
        if n.startswith(pre):
            c.append(pre + 'v' + n[len(pre):])
    m = re.match(r'(.*?)(printf|scanf|syslog)(.*)', n)
    if m:
        c.append(m.group(1) + 'v' + m.group(2) + m.group(3))
    return c


def main():
    mode = sys.argv[1]
    if mode == 'glibc':
        names = scan(os.path.join(sys.argv[2], 'usr', 'include'))
        nm = subprocess.run([sys.argv[3], '-D', '--defined-only', sys.argv[4]],
                            capture_output=True, text=True).stdout
        exports = set()
        for line in nm.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-2] in ('T', 'W', 'i') and '@@' in parts[-1]:
                exports.add(parts[-1].split('@')[0])
    else:
        names = scan(sys.argv[2])
        exports = {line.strip() for line in open(sys.argv[3])}
    rows = []
    for n in sorted(names):
        if n not in exports:
            continue
        vs = [c for c in vname_candidates(n) if c in exports]
        rows.append((n, vs[0] if vs else '-', names[n]))
    for r in rows:
        print('%-28s %-28s %s' % r)
    print('# %d exported variadic functions, %d with a v-variant'
          % (len(rows), sum(1 for r in rows if r[1] != '-')))


main()
