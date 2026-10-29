# gen_init.py: start-up cost of publishing K function addresses, per representation.
# init.il (mscorlib only) and inituco.il (adds UnmanagedCallersOnly). Each phase uses its own set of
# K methods, so no phase sees methods that another phase already prepared.
import os
HERE = os.path.dirname(os.path.abspath(__file__))
K = 1000
# 50 distinct IL signatures for phase D: (return, params)
TYPES = ['int32', 'native int', 'float64', 'float32']
SIGS = []
for n in range(1, 6):
    for i in range(len(TYPES) ** n):
        ps, x = [], i
        for _ in range(n):
            ps.append(TYPES[x % 4]); x //= 4
        SIGS.append(('int32', ps))
        if len(SIGS) == 50:
            break
    if len(SIGS) == 50:
        break


def gen(name, uco):
    hdr = '.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }' if uco else ''
    ucoattr = '.custom instance void [System.Runtime.InteropServices]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )'
    out = []
    w = out.append
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
}}''')
    for j, (r, ps) in enumerate(SIGS):
        w(f'''.class private auto ansi sealed D_s{j} extends [mscorlib]System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {{}}
  .method public hidebysig newslot virtual instance {r} Invoke({', '.join(ps)}) runtime managed {{}}
}}''')
    w('.class private abstract auto ansi sealed beforefieldinit Prog extends [mscorlib]System.Object\n{')
    w('  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_cb(native int f, int32 x) cil managed preservesig {}')
    w('  .field static class [mscorlib]System.Delegate[] keep')
    w('  .field static native int[] fp')
    phases = ['A', 'B', 'C'] + (['E', 'F'] if uco else [])
    for ph in phases:
        for k in range(K):
            attr = ucoattr if ph in 'EF' else ''
            w(f'  .method static int32 {ph}{k}(int32 x) cil managed {{ {attr} .maxstack 2 ldarg.0 ldc.i4 {k} add ret }}')
    for j, (r, ps) in enumerate(SIGS):
        w(f'  .method static int32 S{j}({", ".join(ps)}) cil managed {{ .maxstack 1 ldc.i4 {j} ret }}')
    def ts(label):
        return f'''    ldstr "{label}" call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.0 sub call void Prog::rep(string, int64)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.0'''
    w('''  .method static void rep(string s, int64 t) cil managed
  {
    .maxstack 4
    ldstr "{0,-70} {1,10:F1} us total" ldarg.0 ldarg.1 conv.r8 ldc.r8 1e6 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div box [mscorlib]System.Double
    call void [mscorlib]System.Console::WriteLine(string, object, object) ret
  }
  .method static int32 Main(string[] args) cil managed
  {
    .entrypoint
    .maxstack 8
    .locals (int64 t0, int32 s)
    ldstr "runtime " call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version() callvirt instance string [mscorlib]System.Object::ToString()
    call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4 4000 newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] Prog::keep
    ldc.i4 6000 newarr [mscorlib]System.IntPtr stsfld native int[] Prog::fp
    // warm the helpers once
    ldnull ldftn int32 Prog::S0(int32) newobj instance void D_s0::.ctor(object, native int) call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) pop
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.0''')
    # Phase A: ldftn + store, K functions (today's '<msil>fn_' initialization)
    for k in range(K):
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {k} ldftn int32 Prog::A{k}(int32) stelem.i')
    w(ts(f'A: ldftn + store of {K} functions (Phase 2 shape)'))
    # Phase B: delegate + GetFunctionPointerForDelegate, K functions, one signature
    for k in range(K):
        w(f'    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {k} ldnull ldftn int32 Prog::B{k}(int32) newobj instance void D_i_i::.ctor(object, native int) stelem.ref')
    w(ts(f'B1: {K} delegates (newobj) of one signature'))
    for k in range(K):
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {1000 + k} ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {k} ldelem.ref call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stelem.i')
    w(ts(f'B2: {K} GetFunctionPointerForDelegate of one signature'))
    for k in range(K):
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {1000 + k} ldelem.i ldc.i4.1 call int32 Prog::call_cb(native int, int32) pop')
    w(ts(f'B3: first native call through each of the {K} delegate thunks'))
    for k in range(K):
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {1000 + k} ldelem.i ldc.i4.1 call int32 Prog::call_cb(native int, int32) pop')
    w(ts(f'B4: second native call through each of the {K} delegate thunks'))
    # Phase C: lazily, delegate creation and first call per function interleaved (accessor shape)
    for k in range(K):
        w(f'    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {1000 + k} ldnull ldftn int32 Prog::C{k}(int32) newobj instance void D_i_i::.ctor(object, native int) stelem.ref')
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {2000 + k} ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {1000 + k} ldelem.ref call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stelem.i')
    w(ts(f'C: {K} delegates + GetFunctionPointerForDelegate (another set, warm)'))
    # Phase D: 50 signatures, one function each
    for j, (r, ps) in enumerate(SIGS):
        w(f'    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {2000 + j} ldnull ldftn int32 Prog::S{j}({", ".join(ps)}) newobj instance void D_s{j}::.ctor(object, native int) stelem.ref')
        w(f'    ldsfld native int[] Prog::fp ldc.i4 {3000 + j} ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {2000 + j} ldelem.ref call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stelem.i')
    w(ts('D1: 50 delegates + GetFunctionPointerForDelegate, 50 distinct signatures'))
    for j, (r, ps) in enumerate(SIGS):
        args = ' '.join({'int32': 'ldc.i4.1', 'native int': 'ldc.i4.1 conv.i', 'float64': 'ldc.r8 1.0', 'float32': 'ldc.r4 1.0'}[p] for p in ps)
        w(f'    {args} ldsfld native int[] Prog::fp ldc.i4 {3000 + j} ldelem.i calli unmanaged cdecl int32({", ".join(ps)}) pop')
    w(ts('D2: first call (calli unmanaged) through each of the 50 signatures\' thunks'))
    if uco:
        for k in range(K):
            w(f'    ldsfld native int[] Prog::fp ldc.i4 {4000 + k} ldftn int32 Prog::E{k}(int32) stelem.i')
        w(ts(f'E1: ldftn + store of {K} UCO methods'))
        for k in range(K):
            w(f'    ldsfld native int[] Prog::fp ldc.i4 {4000 + k} ldelem.i ldc.i4.1 call int32 Prog::call_cb(native int, int32) pop')
        w(ts(f'E2: first native call through each of the {K} UCO pointers'))
        for k in range(K):
            w(f'    ldsfld native int[] Prog::fp ldc.i4 {4000 + k} ldelem.i ldc.i4.1 call int32 Prog::call_cb(native int, int32) pop')
        w(ts(f'E3: second native call through each of the {K} UCO pointers'))
    w('    ldc.i4.0 ret\n  }\n}')
    with open(os.path.join(HERE, f'{name}.il'), 'w') as f:
        f.write('\n'.join(out) + '\n')


gen('initfx', False)
gen('inituco', True)
