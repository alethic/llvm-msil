"""Generates an ILAsm program that probes which DllImport module names and
entry points load on the running CLR. Each case is its own method (so a JIT or
type-load failure stays local) called from Main inside try/catch; Main prints
'OK <case>: <result>' or 'FAIL <case>: <exception type>'.

Usage: gen_probe.py windows|linux|modern OUT.il
"""
import sys

STR = ('ldstr "abc"\n    call native int [mscorlib]System.Runtime.InteropServices.'
       'Marshal::StringToHGlobalAnsi(string)')
MEMCHR = STR + '\n    ldc.i4.s 98\n    ldc.i4.3\n    conv.i'
NULLP = 'ldc.i4.0\n    conv.i'

# (case id, module, entry, ret, params, arg-push IL, description)
WIN = [
    ('w01', 'ucrtbase', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs'),
    ('w02', 'ucrtbase.dll', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs'),
    ('w03', 'api-ms-win-crt-utility-l1-1-0', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs via API set'),
    ('w04', 'api-ms-win-crt-utility-l1-1-0.dll', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs via API set + .dll'),
    ('w05', 'API-MS-WIN-CRT-UTILITY-L1-1-0.DLL', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs, upper case'),
    ('w06', 'api-ms-win-crt-string-l1-1-0', 'strlen', 'native int', 'native int', STR, 'strlen via API set'),
    ('w07', 'api-ms-win-crt-math-l1-1-0', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt via API set'),
    ('w08', 'KERNEL32', 'GetTickCount', 'int32', '', '', 'GetTickCount, import-library spelling'),
    ('w09', 'KERNEL32.dll', 'GetTickCount', 'int32', '', '', 'GetTickCount + .dll'),
    ('w10', 'kernel32', 'GetTickCount', 'int32', '', '', 'GetTickCount lower case'),
    ('w11', 'WS2_32', '#9', 'unsigned int16', 'unsigned int16', 'ldc.i4 0x1234', 'htons by ordinal #9'),
    ('w12', 'ws2_32', 'htons', 'unsigned int16', 'unsigned int16', 'ldc.i4 0x1234', 'htons by name'),
    ('w13', 'VCRUNTIME140', 'memchr', 'native int', 'native int, int32, native int', MEMCHR, 'memchr from vcruntime140'),
    ('w14', 'ucrtbase', 'memchr', 'native int', 'native int, int32, native int', MEMCHR, 'memchr from ucrtbase'),
    ('w15', 'ucrtbase', 'frexpf', 'float32', 'float32, native int', 'ldc.r4 1.5\n    ' + NULLP, 'frexpf (not exported)'),
    ('w16', 'api-ms-win-crt-string-l1-1-0', '_strdup', 'native int', 'native int', STR, 'strdup via the oldnames alias _strdup'),
    ('w17', 'libc', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from libc'),
    ('w18', 'msvcrt', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from msvcrt'),
    ('w19', 'api-ms-win-crt-stdio-l1-1-0', '__acrt_iob_func', 'native int', 'unsigned int32', 'ldc.i4.1', '__acrt_iob_func(1) (stdout)'),
    ('w20', 'c', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from c'),
    ('w21', 'api-ms-win-crt-runtime-l1-1-0', '_errno', 'native int', '', '', '_errno()'),
    ('w22', 'api-ms-win-crt-math-l1-1-0', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from the wrong API set (math)'),
    ('w23', 'ucrtbase', 'printf', 'int32', 'native int', STR, 'printf (not exported by ucrtbase)'),
    ('w24', 'ucrtbase', '#2337', 'int32', 'int32', 'ldc.i4.s -5', 'ucrtbase ordinal 2337 (puts) -- skipped output check'),
]
LNX = [
    ('l01', 'libc', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from libc'),
    ('l02', 'c', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from c'),
    ('l03', 'libc.so.6', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from libc.so.6'),
    ('l04', 'libc.so', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from libc.so (dev linker script absent)'),
    ('l05', 'libm', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt from libm'),
    ('l06', 'm', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt from m'),
    ('l07', 'libm.so.6', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt from libm.so.6'),
    ('l08', 'libm.so', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt from libm.so'),
    ('l09', 'libz', 'zlibVersion', 'native int', '', '', 'zlibVersion from libz'),
    ('l10', 'z', 'zlibVersion', 'native int', '', '', 'zlibVersion from z'),
    ('l11', 'libz.so.1', 'zlibVersion', 'native int', '', '', 'zlibVersion from libz.so.1 (DT_SONAME)'),
    ('l12', 'libz.so', 'zlibVersion', 'native int', '', '', 'zlibVersion from libz.so'),
    ('l13', 'libpthread.so.0', 'pthread_self', 'native int', '', '', 'pthread_self from libpthread.so.0 (2.34+: lives in libc)'),
    ('l14', 'libdl.so.2', 'dlerror', 'native int', '', '', 'dlerror from libdl.so.2 (2.34+: lives in libc)'),
    ('l15', 'libm.so.6', 'matherr', 'int32', 'native int', NULLP, 'matherr (compat-only matherr@GLIBC_2.2.5)'),
    ('l16', 'libc.so.6', 'memchr', 'native int', 'native int, int32, native int', MEMCHR, 'memchr (IFUNC)'),
    ('l17', 'libc.so.6', 'strlen', 'native int', 'native int', STR, 'strlen (IFUNC)'),
    ('l18', 'libm.so.6', 'abs', 'int32', 'int32', 'ldc.i4.s -5', 'abs from libm.so.6 (found in its DT_NEEDED libc)'),
    ('l19', 'libm.so.6', 'sqrtf', 'float32', 'float32', 'ldc.r4 2.25', 'sqrtf (default version GLIBC_2.43)'),
    ('l20', 'libm.so.6', 'exp', 'float64', 'float64', 'ldc.r8 0.0', 'exp (exp@@GLIBC_2.29 and exp@GLIBC_2.2.5)'),
    ('l21', 'libc.so.6', '__errno_location', 'native int', '', '', '__errno_location()'),
    ('l22', 'libz.so.1.3.1', 'zlibVersion', 'native int', '', '', 'zlibVersion from the real file name'),
    ('l23', 'libc.so.6', 'fstat', 'int32', 'int32, native int', 'ldc.i4.m1\n    ' + NULLP, 'fstat(-1, NULL) (fstat@@GLIBC_2.33)'),
    ('l24', 'libc.so.6', 'atexit', 'int32', 'native int', NULLP, 'atexit (only in libc_nonshared.a)'),
    ('l25', 'libc', 'sqrt', 'float64', 'float64', 'ldc.r8 2.25', 'sqrt from libc (glibc keeps it in libm)'),
    ('l26', 'libc.so.6', 'errno', 'native int', '', '', 'errno (TLS symbol, GLIBC_PRIVATE) -- resolved only, never meaningful'),
]

WIN_EXTRA = r'''
.method public hidebysig static pinvokeimpl("kernel32" cdecl) native int 'LoadLibraryW'(native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("kernel32" cdecl) native int 'GetProcAddress'(native int, native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("msvcrt" cdecl) native int '__p__environ'() cil managed preservesig {}
.method public static string 'c_x01'() cil managed {
  .maxstack 8
  .locals init (native int h, native int a, native int b)
  ldstr "msvcrt.dll"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalUni(string)
  call native int 'LoadLibraryW'(native int)
  stloc.0
  ldloc.0
  ldstr "_environ"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call native int 'GetProcAddress'(native int, native int)
  stloc.1
  call native int '__p__environ'()
  stloc.2
  ldstr "GetProcAddress(msvcrt, _environ) == __p__environ(): "
  ldloc.1
  ldloc.2
  ceq
  box [mscorlib]System.Boolean
  call string [mscorlib]System.String::Concat(object, object)
  ret
}
'''


def gen(cases, extra_methods='', extra_cases=()):
    out = ['.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }',
           '.assembly probe { .ver 0:0:0:0 }']
    for cid, mod, entry, ret, params, push, desc in cases:
        asn = f' as "{entry}"' if entry else ''
        out.append(f'.method public hidebysig static pinvokeimpl("{mod}"{asn} cdecl) {ret} '
                   f"'p_{cid}'({params}) cil managed preservesig {{}}")
        body = [f".method public static string 'c_{cid}'() cil managed {{",
                '  .maxstack 8', f'  .locals init ({ret} r)']
        if push:
            body.append('    ' + push)
        body += [f"    call {ret} 'p_{cid}'({params})", '    stloc.0', '    ldloca.s r',
                 f'    constrained. {ret}',
                 '    callvirt instance string [mscorlib]System.Object::ToString()', '    ret', '}']
        out += body
    out.append(extra_methods)
    out.append('.method public static int32 main() cil managed {\n  .entrypoint\n  .maxstack 8\n'
               '  .locals init (class [mscorlib]System.Exception e, string s)')
    for cid, mod, entry, ret, params, push, desc in list(cases) + list(extra_cases):
        label = f'{cid} [{mod} as {entry}] {desc}'.replace('"', "'")
        out.append(f'''  .try {{
    call string 'c_{cid}'()
    stloc.1
    ldstr "OK   {label}: "
    ldloc.1
    call string [mscorlib]System.String::Concat(string, string)
    call void [mscorlib]System.Console::WriteLine(string)
    leave L_{cid}
  }} catch [mscorlib]System.Exception {{
    stloc.0
    ldstr "FAIL {label}: "
    ldloc.0
    callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
    callvirt instance string [mscorlib]System.Type::get_FullName()
    call string [mscorlib]System.String::Concat(string, string)
    call void [mscorlib]System.Console::WriteLine(string)
    leave L_{cid}
  }}
L_{cid}:''')
    out.append('  ldc.i4.0\n  ret\n}')
    return '\n'.join(out) + '\n'


which, path = sys.argv[1], sys.argv[2]
if which == 'windows':
    text = gen([c for c in WIN if c[0] != 'w24'], WIN_EXTRA,
               [('x01', 'kernel32', 'GetProcAddress', '', '', '', 'data import address via LoadLibraryW/GetProcAddress')])
elif which == 'linux':
    text = gen(LNX)
else:
    raise SystemExit('unknown kind')
with open(path, 'w', newline='\n') as f:
    f.write(text)
