"""Rewrite i128 udiv/sdiv/urem/srem with a non-constant divisor into compiler-rt helper calls.

usage: python -I i128_to_calls.py in.ll out.ll
Constant divisors are left alone (LLVM expands those natively without a helper too).
"""
import re, sys

src = open(sys.argv[1]).read()
names = {'udiv': '__udivti3', 'sdiv': '__divti3', 'urem': '__umodti3', 'srem': '__modti3'}
used = set()
n = 0


def rep(m):
    global n
    res, op, a, b = m.group(1), m.group(2), m.group(3), m.group(4)
    if re.fullmatch(r'-?\d+', b):
        return m.group(0)
    used.add(names[op])
    n += 1
    return f'{res} = call i128 @{names[op]}(i128 {a}, i128 {b})'


out = re.sub(r'(%[\w.]+) = (udiv|sdiv|urem|srem)(?: exact)? i128 ([%\w.-]+), ([%\w.-]+)', rep, src)
decls = ''.join(f'declare i128 @{u}(i128, i128)\n' for u in sorted(used))
open(sys.argv[2], 'w').write(out + '\n' + decls)
print(f'rewrote {n} sites: {sorted(used)}')
