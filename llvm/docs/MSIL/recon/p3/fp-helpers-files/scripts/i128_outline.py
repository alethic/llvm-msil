"""Variant of i128_to_calls.py: call per-module linkonce_odr hidden noinline functions
(@__msil_udiv128, ...) whose bodies are the plain IR instruction, so ExpandIRInsts expands each
operation kind once per module (helpers-switches C2), instead of calling compiler-rt."""
import re, sys
src = open(sys.argv[1]).read()
names = {'udiv': '__msil_udiv128', 'sdiv': '__msil_sdiv128', 'urem': '__msil_urem128', 'srem': '__msil_srem128'}
used = set(); n = 0
def rep(m):
    global n
    res, op, a, b = m.group(1), m.group(2), m.group(3), m.group(4)
    if re.fullmatch(r'-?\d+', b):
        return m.group(0)
    used.add(op); n += 1
    return f'{res} = call i128 @{names[op]}(i128 {a}, i128 {b})'
out = re.sub(r'(%[\w.]+) = (udiv|sdiv|urem|srem)(?: exact)? i128 ([%\w.-]+), ([%\w.-]+)', rep, src)
defs = ''
for op in sorted(used):
    defs += (f'$"{names[op]}" = comdat any\n'
             f'define linkonce_odr hidden i128 @{names[op]}(i128 %a, i128 %b) noinline comdat {{\n'
             f'  %r = {op} i128 %a, %b\n  ret i128 %r\n}}\n')
open(sys.argv[2], 'w').write(out + '\n' + defs)
print(f'outlined {n} sites: {sorted(used)}')
