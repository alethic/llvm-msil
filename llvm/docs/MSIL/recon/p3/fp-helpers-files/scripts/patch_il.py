"""Replace the bodies of named <Module> methods in an ildasm listing.

usage: python -I patch_il.py <in.il> <out.il> <recipe>
recipe 'half': __extendhfsf2/__truncsfhf2/__truncdfhf2 -> System.Half (net5+ BitConverter/Half members)
recipe 'int128': __udivti3/__umodti3/__divti3/__modti3 (SysV form: sret, lo, hi, lo, hi) -> System.UInt128/Int128
"""
import re, sys

SR = '[System.Runtime]'
EXTERN = '''.assembly extern System.Runtime
{
  .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
'''

HALF = {
    '__extendhfsf2': '''  .maxstack 1
  ldarg.0
  conv.i2
  call valuetype %(SR)sSystem.Half %(SR)sSystem.BitConverter::Int16BitsToHalf(int16)
  call float32 %(SR)sSystem.Half::op_Explicit(valuetype %(SR)sSystem.Half)
  ret''',
    '__truncsfhf2': '''  .maxstack 1
  ldarg.0
  call valuetype %(SR)sSystem.Half %(SR)sSystem.Half::op_Explicit(float32)
  call int16 %(SR)sSystem.BitConverter::HalfToInt16Bits(valuetype %(SR)sSystem.Half)
  conv.u2
  ret''',
    '__truncdfhf2': '''  .maxstack 1
  ldarg.0
  call valuetype %(SR)sSystem.Half %(SR)sSystem.Half::op_Explicit(float64)
  call int16 %(SR)sSystem.BitConverter::HalfToInt16Bits(valuetype %(SR)sSystem.Half)
  conv.u2
  ret''',
}

U = 'valuetype %(SR)sSystem.UInt128' % {'SR': SR}
I = 'valuetype %(SR)sSystem.Int128' % {'SR': SR}


def i128(op, ty, sign):
    T = U if not sign else I
    full = '%sSystem.%s' % (SR, 'Int128' if sign else 'UInt128')
    return '''  .maxstack 4
  .locals init (%(T)s V_a, %(T)s V_b, %(T)s V_r)
  ldloca.s V_a
  ldarg.2
  conv.u8
  ldarg.1
  conv.u8
  call instance void %(full)s::.ctor(uint64, uint64)
  ldloca.s V_b
  ldarg.s 4
  conv.u8
  ldarg.3
  conv.u8
  call instance void %(full)s::.ctor(uint64, uint64)
  ldloc.0
  ldloc.1
  call %(T)s %(full)s::%(op)s(%(T)s, %(T)s)
  stloc.2
  ldarg.0
  ldloc.2
  call uint64 %(full)s::op_Explicit(%(T)s)
  stind.i8
  ldarg.0
  ldc.i4.8
  add
  ldloc.2
  ldc.i4.s 64
  call %(T)s %(full)s::op_RightShift(%(T)s, int32)
  call uint64 %(full)s::op_Explicit(%(T)s)
  stind.i8
  ret''' % {'T': T, 'full': full, 'op': op}


INT128 = {
    '__udivti3': i128('op_Division', U, False),
    '__umodti3': i128('op_Modulus', U, False),
    '__divti3': i128('op_Division', I, True),
    '__modti3': i128('op_Modulus', I, True),
}


def main():
    src, dst, recipe = sys.argv[1:4]
    txt = open(src, encoding='utf-8', errors='replace').read()
    table = {'half': HALF, 'int128': INT128}[recipe]
    n = 0
    for name, body in table.items():
        body = body % {'SR': SR} if '%(SR)s' in body else body
        pat = re.compile(r'(\.method [^\n]*?\b' + re.escape(name) + r'\([^)]*\)\s*cil managed\s*\{)(.*?)(\} // end of (?:global )?method)', re.S)
        txt, k = pat.subn(lambda m: m.group(1) + '\n' + body + '\n' + m.group(3), txt, count=1)
        n += k
        if not k:
            print('not found:', name, file=sys.stderr)
    txt = txt.replace('.assembly extern mscorlib', EXTERN + '.assembly extern mscorlib', 1)
    open(dst, 'w', encoding='utf-8').write(txt)
    print('patched', n)


main()
