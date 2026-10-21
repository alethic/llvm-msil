# Synthetic option-2 objects for a link-throughput measurement: NOBJ objects x NF functions; every
# function calls 3 functions of other objects (ImplMap stubs) and reads 2 globals of other objects
# (MemberRef placeholders); every object has NG globals and a table of function pointers (data relocs).
import json, os, random, sys
NOBJ, NF, NG = int(sys.argv[1]), int(sys.argv[2]), 10
random.seed(1)
hdr = """.module '{m}'
.assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
.assembly extern 'msil.extern' {{ .ver 0:0:0:0 }}
.module extern 'msil.unresolved'
.mresource private 'msil.link' {{ }}
"""
for o in range(NOBJ):
    d = f'o{o}'; os.makedirs(d, exist_ok=True)
    lines = [hdr.format(m=f'o{o}.o')]
    stubs = set()
    for f in range(NF):
        body = ['  .maxstack 4', '  ldarg.0']
        for k in range(3):
            t = (o + 1 + random.randrange(NOBJ - 1)) % NOBJ
            fn = f'f{t}_{random.randrange(NF)}'
            stubs.add(fn)
            body += [f"  ldc.i4.1", f"  call int32 '{fn}'(int32)", '  add']
        for k in range(2):
            t = (o + 1 + random.randrange(NOBJ - 1)) % NOBJ
            body += [f"  ldsfld native int [msil.extern]'<msil>Symbols'::'g{t}_{random.randrange(NG)}'", '  ldind.i4', '  add']
        body += [f"  ldsfld native int 'g{o}_0'", '  ldind.i4', '  add', '  ret']
        # recursion guard: only 'leaf' functions are executed; this measures linking, not running
        lines.append(f".method public static int32 'f{o}_{f}'(int32) cil managed\n{{\n" + '\n'.join(body) + f"\n}} // end of method 'f{o}_{f}'\n")
    for s in sorted(stubs):
        lines.append(f'.method public hidebysig static pinvokeimpl("msil.unresolved" cdecl) int32 \'{s}\'(int32) cil managed preservesig {{}}')
    syms, relocs, img = [], [], bytearray()
    for g in range(NG):
        lines.append(f".field public static initonly native int 'g{o}_{g}'")
        syms.append(dict(name=f'g{o}_{g}', kind='data', offset=g * 4, size=4, align=4, bind='global', comdat=None, merge_strings=False))
        img += (g).to_bytes(4, 'little')
    tab = len(img)
    lines.append(f".field public static initonly native int 'tab{o}'")
    syms.append(dict(name=f'tab{o}', kind='data', offset=tab, size=8 * 8, align=8, bind='global', comdat=None, merge_strings=False))
    img += bytes(64)
    for k in range(8):
        relocs.append(dict(site=tab + 8 * k, kind='fnaddr64', target=f'f{(o + k) % NOBJ}_{k % NF}', addend=0))
    for f in range(NF):
        syms.append(dict(name=f'f{o}_{f}', kind='func', bind='global', comdat=None))
    if o == 0:
        lines.append(".method public static int32 'main'() cil managed\n{\n  .maxstack 1\n  ldc.i4.0\n  ret\n}")
        syms.append(dict(name='main', kind='func', bind='global', comdat=None))
    open(f'{d}/o{o}.o.il', 'w').write('\n'.join(lines) + '\n')
    json.dump(dict(object=f'o{o}.o', image=img.hex(), init_size=len(img), symbols=syms, relocs=relocs, ctors=[], aliases={}, weak_undef=[]),
              open(f'{d}/msil.link', 'w'))
