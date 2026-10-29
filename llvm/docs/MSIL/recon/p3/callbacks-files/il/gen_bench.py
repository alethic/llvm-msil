# gen_bench.py: per-call cost of every way a C indirect call or a native callback can be made.
# Writes bench.il (mscorlib only: Framework 4.8, .NET 8, .NET 10) and benchuco.il (adds the
# [UnmanagedCallersOnly] cases; modern .NET only). Usage: bench.exe [n]
import os
HERE = os.path.dirname(os.path.abspath(__file__))


def gen(name, uco):
    hdr = '.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }' if uco else ''
    ucoattr = '.custom instance void [System.Runtime.InteropServices]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )'
    cases = []

    def loop(label, body, setup=''):
        # body: IL that leaves an int32 on the stack, with the loop counter in local i (int32)
        cases.append((label, setup, body))

    # --- managed-side indirect call shapes (what a C indirect call would compile to)
    loop('direct call', 'ldloc.s i call int32 Prog::add1(int32)')
    loop('managed calli (today: ldftn value)', 'ldloc.s i ldsfld native int Prog::fp_ldftn calli int32(int32)')
    loop('calli unmanaged -> delegate thunk (A1 default)', 'ldloc.s i ldsfld native int Prog::fp_del calli unmanaged cdecl int32(int32)')
    if uco:
        loop('calli unmanaged -> UCO thunk (A1 switch)', 'ldloc.s i ldsfld native int Prog::fp_uco calli unmanaged cdecl int32(int32)')
    loop('calli unmanaged -> native function', 'ldloc.s i ldsfld native int Prog::fp_native calli unmanaged cdecl int32(int32)')
    # A2: exact own-table hash probe, inline, then managed calli; miss -> calli unmanaged.
    probe = lambda fld: f'''ldsfld native int Prog::{fld} stloc.s p
    ldloc.s p call native int Prog::'<msil>fnmgd'(native int) dup stloc.s q
    brfalse.s MISS_%
    ldloc.s i ldloc.s q calli int32(int32) br.s DONE_%
MISS_%: ldloc.s i ldloc.s p calli unmanaged cdecl int32(int32)
DONE_%:'''
    loop('A2 lookup helper hit (delegate value) + managed calli', probe('fp_del'))
    loop('A2 lookup helper miss (native value) + calli unmanaged', probe('fp_native'))
    inl = lambda fld: f'''ldsfld native int Prog::{fld} stloc.s p
    // h = ((p >> 4) ^ (p >> 20)) & mask; entries of 16 bytes {{pub, mgd}}
    ldsfld native int Prog::tab ldloc.s p ldc.i4.4 shr.un ldloc.s p ldc.i4.s 20 shr.un xor ldsfld native int Prog::mask and ldc.i4.4 shl add stloc.s q
    ldloc.s q ldind.i ldloc.s p bne.un.s SLOW_%
    ldloc.s i ldloc.s q ldc.i4.8 add ldind.i calli int32(int32) br.s DONE_%
SLOW_%: ldloc.s p call native int Prog::'<msil>fnmgd'(native int) dup stloc.s q brfalse.s NAT_%
    ldloc.s i ldloc.s q calli int32(int32) br.s DONE_%
NAT_%: ldloc.s i ldloc.s p calli unmanaged cdecl int32(int32)
DONE_%:'''
    loop('A2 inline first probe hit (delegate value) + managed calli', inl('fp_del'))
    loop('A2 inline first probe miss (native value) + calli unmanaged', inl('fp_native'))
    ic = lambda fld: f'''ldsfld native int Prog::{fld} stloc.s p
    // per-site cache: the table entry address that matched last time
    ldsfld native int Prog::ic_site ldind.i ldloc.s p bne.un.s SLOW_%
    ldloc.s i ldsfld native int Prog::ic_site ldc.i4.8 add ldind.i calli int32(int32) br.s DONE_%
SLOW_%: ldloc.s i ldloc.s p calli unmanaged cdecl int32(int32)
DONE_%:'''
    loop('A2 per-site cache hit + managed calli', ic('fp_del'))

    native_loops = [('native loop -> native function', 'ldloc.3 call int64 Prog::loop_native(int32)'),
                    ('native loop -> delegate thunk', 'ldsfld native int Prog::fp_del ldloc.3 call int64 Prog::loop_cb(native int, int32)'),
                    ('native loop -> ldftn value (unsafe, for scale)', 'ldsfld native int Prog::fp_ldftn ldloc.3 call int64 Prog::loop_cb(native int, int32)')]
    if uco:
        native_loops.append(('native loop -> UCO thunk', 'ldsfld native int Prog::fp_uco ldloc.3 call int64 Prog::loop_cb(native int, int32)'))

    body = []
    for k, (label, setup, b) in enumerate(cases):
        b = b.replace('%', str(k))
        body.append(f'''    // {label}
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.s t0
    ldc.i4.0 conv.i8 stloc.2 ldc.i4.0 stloc.s i
L{k}: {b}
    conv.i8 ldloc.2 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldloc.3 blt L{k}
    ldstr "{label}" call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.s t0 sub ldloc.3 call void Prog::bench(string, int64, int32)
    ldloc.2 ldsfld int64 Prog::check add stsfld int64 Prog::check
''')
    for k, (label, b) in enumerate(native_loops):
        body.append(f'''    // {label}
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.s t0
    {b} ldsfld int64 Prog::check add stsfld int64 Prog::check
    ldstr "{label}" call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.s t0 sub ldloc.3 call void Prog::bench(string, int64, int32)
''')
    # qsort of n/20 ints with each comparator kind (ms per sort, and ns per comparison)
    qs = [('qsort native comparator', 'call native int Prog::get_native_cmp()'),
          ('qsort delegate-thunk comparator', 'ldsfld native int Prog::fp_cmp_del')]
    if uco:
        qs.append(('qsort UCO comparator', 'ldftn int32 Prog::\'<msil>uco_cmp\'(native int, native int)'))
    for k, (label, fp) in enumerate(qs):
        body.append(f'''    // {label}
    ldloc.3 ldc.i4.s 20 div call void Prog::fill(int32)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.s t0
    ldsfld native int Prog::arr ldloc.3 ldc.i4.s 20 div conv.i ldc.i4.4 conv.i {fp} call void Prog::do_qsort(native int, native int, native int, native int)
    ldstr "{label} (per sort of n/20 ints, ns per element)" call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.s t0 sub ldloc.3 ldc.i4.s 20 div call void Prog::bench(string, int64, int32)
    call void Prog::checksorted()
''')
    uco_methods = f'''
  .method assembly static int32 '<msil>uco_add1'(int32 x) cil managed
  {{ {ucoattr} .maxstack 1 ldarg.0 call int32 Prog::add1(int32) ret }}
  .method assembly static int32 '<msil>uco_cmp'(native int a, native int b) cil managed
  {{ {ucoattr} .maxstack 2 ldarg.0 ldarg.1 call int32 Prog::cmp_int(native int, native int) ret }}
''' if uco else ''
    uco_init = "ldftn int32 Prog::'<msil>uco_add1'(int32) stsfld native int Prog::fp_uco" if uco else ''
    il = f'''// {name}.il (generated by gen_bench.py)
.assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
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
.class private auto ansi sealed D_cmp extends [mscorlib]System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {{}}
  .method public hidebysig newslot virtual instance int32 Invoke(native int a, native int b) runtime managed {{}}
}}
.class private abstract auto ansi sealed beforefieldinit Prog extends [mscorlib]System.Object
{{
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int64 loop_cb(native int f, int32 n) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int get_native_fn() cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int get_native_cmp() cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int64 loop_native(int32 n) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) void do_qsort(native int b, native int n, native int sz, native int cmp) cil managed preservesig {{}}
  .field static native int fp_ldftn
  .field static native int fp_del
  .field static native int fp_uco
  .field static native int fp_native
  .field static native int fp_cmp_del
  .field static native int tab
  .field static native int mask
  .field static native int ic_site
  .field static native int arr
  .field static int64 check
  .field static class [mscorlib]System.Delegate[] keep
  .method static int32 add1(int32 x) cil managed {{ .maxstack 2 ldarg.0 ldc.i4.1 add ret }}
  .method static int32 cmp_int(native int a, native int b) cil managed
  {{ .maxstack 3 ldarg.0 ldind.i4 ldarg.1 ldind.i4 cgt ldarg.0 ldind.i4 ldarg.1 ldind.i4 clt sub ret }}
{uco_methods}
  // The exact own-table lookup: native-callable value -> managed pointer, 0 when not ours.
  // Open addressing over 16-byte entries {{pub, mgd}}; a 0 key ends a probe chain.
  .method assembly static native int '<msil>fnmgd'(native int p) cil managed
  {{
    .maxstack 4
    .locals (native int e, native int k)
    ldsfld native int Prog::tab ldarg.0 ldc.i4.4 shr.un ldarg.0 ldc.i4.s 20 shr.un xor ldsfld native int Prog::mask and ldc.i4.4 shl add stloc.0
LOOP: ldloc.0 ldind.i dup stloc.1 ldarg.0 bne.un.s NEXT
    ldloc.0 ldc.i4.8 add ldind.i ret
NEXT: ldloc.1 brtrue.s STEP
    ldc.i4.0 conv.i ret
STEP: ldloc.0 ldc.i4.s 16 add stloc.0
    // wrap around
    ldloc.0 ldsfld native int Prog::tab ldsfld native int Prog::mask ldc.i4.1 add ldc.i4.4 shl add blt.un.s LOOP
    ldsfld native int Prog::tab stloc.0 br.s LOOP
  }}
  .method static void insert(native int pub, native int mgd) cil managed
  {{
    .maxstack 4
    .locals (native int e)
    ldsfld native int Prog::tab ldarg.0 ldc.i4.4 shr.un ldarg.0 ldc.i4.s 20 shr.un xor ldsfld native int Prog::mask and ldc.i4.4 shl add stloc.0
LOOP: ldloc.0 ldind.i brfalse.s PUT
    ldloc.0 ldc.i4.s 16 add stloc.0
    ldloc.0 ldsfld native int Prog::tab ldsfld native int Prog::mask ldc.i4.1 add ldc.i4.4 shl add blt.un.s LOOP
    ldsfld native int Prog::tab stloc.0 br.s LOOP
PUT: ldloc.0 ldc.i4.8 add ldarg.1 stind.i
    ldloc.0 ldarg.0 stind.i
    ret
  }}
  .method static void fill(int32 n) cil managed
  {{
    .maxstack 4
    .locals (int32 i, int32 x)
    ldsfld native int Prog::arr brtrue.s HAVE
    ldc.i4 4000000 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stsfld native int Prog::arr
HAVE: ldc.i4 12345 stloc.1 ldc.i4.0 stloc.0
L: ldloc.1 ldc.i4 1103515245 mul ldc.i4 12345 add stloc.1
    ldsfld native int Prog::arr ldloc.0 ldc.i4.4 mul add ldloc.1 ldc.i4.8 shr.un stind.i4
    ldloc.0 ldc.i4.1 add dup stloc.0 ldarg.0 blt.s L
    ldarg.0 stsfld int32 Prog::nfill
    ret
  }}
  .field static int32 nfill
  .method static void checksorted() cil managed
  {{
    .maxstack 4
    .locals (int32 i)
    ldc.i4.1 stloc.0
L: ldloc.0 ldsfld int32 Prog::nfill bge.s OK
    ldsfld native int Prog::arr ldloc.0 ldc.i4.1 sub ldc.i4.4 mul add ldind.u4
    ldsfld native int Prog::arr ldloc.0 ldc.i4.4 mul add ldind.u4 ble.un.s NEXT
    ldstr "NOT SORTED" call void [mscorlib]System.Console::WriteLine(string) ret
NEXT: ldloc.0 ldc.i4.1 add stloc.0 br.s L
OK: ret
  }}
  .method static void bench(string name, int64 ticks, int32 n) cil managed
  {{
    .maxstack 4
    ldstr "{{0,-62}} {{1,8:F2}} ns"
    ldarg.0
    ldarg.1 conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldarg.2 conv.r8 div box [mscorlib]System.Double
    call void [mscorlib]System.Console::WriteLine(string, object, object)
    ret
  }}
  .method static native int keepalive(class [mscorlib]System.Delegate d) cil managed
  {{
    .maxstack 4
    ldsfld class [mscorlib]System.Delegate[] Prog::keep brtrue.s HAVE
    ldc.i4 16 newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] Prog::keep
HAVE: ldsfld class [mscorlib]System.Delegate[] Prog::keep ldsfld int32 Prog::nkeep ldarg.0 stelem.ref
    ldsfld int32 Prog::nkeep ldc.i4.1 add stsfld int32 Prog::nkeep
    ldarg.0 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) ret
  }}
  .field static int32 nkeep
  .method static int32 Main(string[] args) cil managed
  {{
    .entrypoint
    .maxstack 8
    .locals (string t, native int p, int64 s, int32 n, int32 i, native int q, int64 t0, int32 rep)
    ldc.i4 20000000 stloc.3
    ldarg.0 ldlen conv.i4 ldc.i4.1 blt.s NODEF
    ldarg.0 ldc.i4.0 ldelem.ref call int32 [mscorlib]System.Int32::Parse(string) stloc.3
NODEF:
    ldstr "runtime " call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version() callvirt instance string [mscorlib]System.Object::ToString()
    call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    // the table: 1024 entries, mask 1023
    ldc.i4 16384 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stsfld native int Prog::tab
    ldsfld native int Prog::tab ldc.i4.0 ldc.i4 16384 initblk
    ldc.i4 1023 conv.i stsfld native int Prog::mask
    ldftn int32 Prog::add1(int32) stsfld native int Prog::fp_ldftn
    ldnull ldftn int32 Prog::add1(int32) newobj instance void D_i_i::.ctor(object, native int) call native int Prog::keepalive(class [mscorlib]System.Delegate) stsfld native int Prog::fp_del
    ldnull ldftn int32 Prog::cmp_int(native int, native int) newobj instance void D_cmp::.ctor(object, native int) call native int Prog::keepalive(class [mscorlib]System.Delegate) stsfld native int Prog::fp_cmp_del
    {uco_init}
    call native int Prog::get_native_fn() stsfld native int Prog::fp_native
    ldsfld native int Prog::fp_del ldftn int32 Prog::add1(int32) call void Prog::insert(native int, native int)
    // the per-site cache points at the entry of fp_del
    ldc.i4.s 16 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stsfld native int Prog::ic_site
    ldsfld native int Prog::ic_site ldsfld native int Prog::fp_del stind.i
    ldsfld native int Prog::ic_site ldc.i4.8 add ldftn int32 Prog::add1(int32) stind.i
    ldc.i4.0 stloc.s rep
REP:
    ldstr "--- round" call void [mscorlib]System.Console::WriteLine(string)
{''.join(body)}
    ldloc.s rep ldc.i4.1 add dup stloc.s rep ldc.i4.3 blt REP
    ldsfld int64 Prog::check call void [mscorlib]System.Console::WriteLine(int64)
    ldc.i4.0 ret
  }}
}}
'''
    with open(os.path.join(HERE, f'{name}.il'), 'w') as f:
        f.write(il)


gen('bench', False)
gen('benchuco', True)
