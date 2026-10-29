# gen_cctor.py: the module-initializer cost of publishing K function addresses, JIT time included.
# Each init method runs once (first call = JIT + execution), on its own set of K target methods.
#   initA: ldftn + stsfld per function (the Phase 2 '<msil>fn_' shape)
#   initB: ldnull, ldftn, newobj D, call GetFunctionPointerForDelegate, keep-alive store, stsfld per function
#   initC: table-driven: ldftn into a native int[] per function, then one loop that creates the delegates
#   initU: ldftn of a UCO thunk + stsfld per function (inituco only)
import os
HERE = os.path.dirname(os.path.abspath(__file__))
K = int(os.environ.get('K', '1000'))


def gen(name, uco):
    hdr = '.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }' if uco else ''
    ucoattr = '.custom instance void [System.Runtime.InteropServices]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )'
    o = []
    w = o.append
    w(f'''.assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
.assembly extern System {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
{hdr}
.assembly {name} {{ .ver 0:0:0:0 }}
.module {name}.exe
.corflags 0x00000001
.class private auto ansi sealed D_i_i extends [mscorlib]System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {{}}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {{}}
}}
.class private abstract auto ansi sealed beforefieldinit Prog extends [mscorlib]System.Object
{{
  .field static class [mscorlib]System.Delegate[] keep
  .field static native int[] tmp''')
    sets = ['A', 'B', 'C', 'E'] + (['U'] if uco else [])
    for s in sets:
        for k in range(K):
            w(f'  .field static native int fn{s}{k}')
            a = ucoattr if s == 'U' else ''
            w(f'  .method static int32 {s}{k}(int32 x) cil managed {{ {a} .maxstack 2 ldarg.0 ldc.i4 {k} add ret }}')
    # initA
    w('  .method static void initA() cil managed noinlining {\n    .maxstack 2')
    for k in range(K):
        w(f'    ldftn int32 Prog::A{k}(int32) stsfld native int Prog::fnA{k}')
    w('    ret }')
    # initB
    w('  .method static void initB() cil managed noinlining {\n    .maxstack 4')
    for k in range(K):
        w(f'    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {k} ldnull ldftn int32 Prog::B{k}(int32) newobj instance void D_i_i::.ctor(object, native int) dup stloc.0 stelem.ref ldloc.0 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stsfld native int Prog::fnB{k}')
    w('    ret }')
    o[-1 - K - 1] = o[-1 - K - 1].replace('.maxstack 4', '.maxstack 4\n    .locals (class [mscorlib]System.Delegate d)')
    # initC: table-driven
    w('  .method static void initC() cil managed noinlining {\n    .maxstack 5\n    .locals (int32 i, class [mscorlib]System.Delegate d)')
    w(f'    ldc.i4 {K} newarr [mscorlib]System.IntPtr stsfld native int[] Prog::tmp')
    for k in range(K):
        w(f'    ldsfld native int[] Prog::tmp ldc.i4 {k} ldftn int32 Prog::C{k}(int32) stelem.i')
    w(f'''    ldc.i4.0 stloc.0
L:  ldnull ldsfld native int[] Prog::tmp ldloc.0 ldelem.i newobj instance void D_i_i::.ctor(object, native int) stloc.1
    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldloc.0 ldc.i4 {K} add ldloc.1 stelem.ref
    ldsfld native int[] Prog::tmp ldloc.0 ldloc.1 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stelem.i
    ldloc.0 ldc.i4.1 add dup stloc.0 ldc.i4 {K} blt.s L''')
    w('    ret }')
    # initE: the straight-line shape of initB, split into 20 methods of 50 functions each
    CH = 50
    for c in range(0, K, CH):
        w(f'  .method static void initE_{c}() cil managed noinlining {{\n    .maxstack 4\n    .locals (class [mscorlib]System.Delegate d)')
        for k in range(c, min(K, c + CH)):
            w(f'    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {2 * K + k} ldnull ldftn int32 Prog::E{k}(int32) newobj instance void D_i_i::.ctor(object, native int) dup stloc.0 stelem.ref ldloc.0 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stsfld native int Prog::fnE{k}')
        w('    ret }')
    w('  .method static void initE() cil managed noinlining {\n    .maxstack 1')
    for c in range(0, K, CH):
        w(f'    call void Prog::initE_{c}()')
    w('    ret }')
    if uco:
        w('  .method static void initU() cil managed noinlining {\n    .maxstack 2')
        for k in range(K):
            w(f'    ldftn int32 Prog::U{k}(int32) stsfld native int Prog::fnU{k}')
        w('    ret }')
    w('''  .method static void rep(string s, int64 t) cil managed
  {
    .maxstack 4
    ldstr "{0,-60} {1,10:F1} us" ldarg.0 ldarg.1 conv.r8 ldc.r8 1e6 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div box [mscorlib]System.Double
    call void [mscorlib]System.Console::WriteLine(string, object, object) ret
  }
  .method static int32 Main(string[] args) cil managed
  {
    .entrypoint
    .maxstack 4
    .locals (int64 t0)
    ldstr "runtime " call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version() callvirt instance string [mscorlib]System.Object::ToString()
    call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)''')
    w(f'    ldc.i4 {3 * K} newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] Prog::keep')
    w('    ldnull ldftn int32 Prog::A0(int32) newobj instance void D_i_i::.ctor(object, native int) call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) pop')
    for s, label in [('A', 'ldftn + stsfld per function (Phase 2 fn_ shape)'),
                     ('B', 'straight-line delegate + GFPFD per function'),
                     ('C', 'ldftn table + one delegate loop'),
                     ('E', 'straight-line delegate + GFPFD, 20 methods of 50')] + ([('U', 'ldftn of UCO thunk + stsfld per function')] if uco else []):
        w(f'''    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.0
    call void Prog::init{s}()
    ldstr "init{s} (first call, JIT included): {K} x {label}" call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.0 sub call void Prog::rep(string, int64)''')
    w('    ldc.i4.0 ret\n  }\n}')
    with open(os.path.join(HERE, f'{name}.il'), 'w') as f:
        f.write('\n'.join(o) + '\n')


gen('cctorfx', False)
gen('cctoruco', True)
