# patch_fp.py <in.il> <out.il> <delegate|uco>
# Rewrites clang -S output (Phase 2 shape: '<msil>fn_' fields set with ldftn, managed calli) into the shape
# this report proposes for P3-B:
#   - every '<msil>fn_<f>' holds a native-callable value: a kept-alive delegate thunk (default) or the
#     ldftn of an [UnmanagedCallersOnly] thunk '<msil>uco_<f>' (switch);
#   - the module initializer registers (native-callable value -> managed ldftn) in the assembly's table;
#   - every calli becomes a call of a per-signature dispatcher: table hit -> managed calli of the C function,
#     miss -> calli unmanaged cdecl (a native function pointer).
import re
import sys

src, dst, mode = sys.argv[1], sys.argv[2], sys.argv[3]
il = open(src, encoding='utf-8-sig').read()

# The address-taken functions and their signatures, from the .cctor's ldftn/stsfld pairs.
pairs = re.findall(r"  ldftn (\S+(?: \S+)?) '([^']+)'\(([^)]*)\)\n  stsfld native int '<msil>fn_([^']+)'", il)
funcs = []  # (ret, name, params)
for ret, name, params, fld in pairs:
    assert name == fld, (name, fld)
    funcs.append((ret, name, params))

sigs = {}
for ret, name, params in funcs:
    sigs.setdefault((ret, params), len(sigs))

ucoattr = ('.custom instance void System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() '
           '= ( 01 00 00 00 )')
extra = []
if mode == 'delegate':
    for (ret, params), j in sigs.items():
        extra.append(f""".class private auto ansi sealed '<msil>d_{j}' extends [mscorlib]System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {{}}
  .method public hidebysig newslot virtual instance {ret} Invoke({params}) runtime managed {{}}
}}""")
else:
    # The private attribute class that CoreCLR matches by name (measured: uconame.il).
    extra.append(""".class private auto ansi sealed beforefieldinit System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute extends [mscorlib]System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret }
}""")
    for ret, name, params in funcs:
        ps = [p.strip() for p in params.split(',')] if params.strip() else []
        loads = ' '.join(f'ldarg.s {i}' for i in range(len(ps)))
        extra.append(f""".method assembly static {ret} '<msil>uco_{name}'({params}) cil managed
{{
  {ucoattr}
  .maxstack {max(1, len(ps))}
  {loads} call {ret} '{name}'({params}) ret
}}""")

# The table: open addressing over {pub, mgd} pairs in unmanaged memory; the lookup and the insert.
extra.append("""
.field private static native int '<msil>fntab'
.field private static native int '<msil>fnmask'
.field private static class [mscorlib]System.Delegate[] '<msil>thunks'
.field private static int32 '<msil>hits'
.field private static int32 '<msil>misses'
.method assembly static native int '<msil>fnmgd'(native int p) cil managed
{
  .maxstack 4
  .locals (native int e, native int k)
  ldsfld native int '<msil>fntab' brfalse.s NONE
  ldsfld native int '<msil>fntab' ldarg.0 ldc.i4.3 shr.un ldarg.0 ldc.i4.s 17 shr.un xor ldsfld native int '<msil>fnmask' and ldc.i4.4 shl add stloc.0
LOOP: ldloc.0 ldind.i dup stloc.1 ldarg.0 bne.un.s NEXT
  ldloc.0 ldc.i4.8 add ldind.i ret
NEXT: ldloc.1 brfalse.s NONE
  ldloc.0 ldc.i4.s 16 add stloc.0
  ldloc.0 ldsfld native int '<msil>fntab' ldsfld native int '<msil>fnmask' ldc.i4.1 add ldc.i4.4 shl add blt.un.s LOOP
  ldsfld native int '<msil>fntab' stloc.0 br.s LOOP
NONE: ldc.i4.0 conv.i ret
}
.method assembly static void '<msil>fnadd'(native int pub, native int mgd) cil managed
{
  .maxstack 4
  .locals (native int e)
  ldsfld native int '<msil>fntab' brtrue.s HAVE
  // 256 entries of 16 bytes; a real table is sized from the number of address-taken functions
  ldc.i4 4096 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stsfld native int '<msil>fntab'
  ldsfld native int '<msil>fntab' ldc.i4.0 ldc.i4 4096 initblk
  ldc.i4 255 conv.i stsfld native int '<msil>fnmask'
HAVE: ldsfld native int '<msil>fntab' ldarg.0 ldc.i4.3 shr.un ldarg.0 ldc.i4.s 17 shr.un xor ldsfld native int '<msil>fnmask' and ldc.i4.4 shl add stloc.0
LOOP: ldloc.0 ldind.i brfalse.s PUT
  ldloc.0 ldc.i4.s 16 add stloc.0
  ldloc.0 ldsfld native int '<msil>fntab' ldsfld native int '<msil>fnmask' ldc.i4.1 add ldc.i4.4 shl add blt.un.s LOOP
  ldsfld native int '<msil>fntab' stloc.0 br.s LOOP
PUT: ldloc.0 ldc.i4.8 add ldarg.1 stind.i
  ldloc.0 ldarg.0 stind.i
  ret
}
.method assembly static native int '<msil>mkthunk'(class [mscorlib]System.Delegate d, int32 i, native int mgd) cil managed
{
  .maxstack 4
  .locals (native int pub)
  ldsfld class [mscorlib]System.Delegate[] '<msil>thunks' brtrue.s HAVE
  ldc.i4 NFUNCS newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] '<msil>thunks'
HAVE: ldsfld class [mscorlib]System.Delegate[] '<msil>thunks' ldarg.1 ldarg.0 stelem.ref
  ldarg.0 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.0
  ldloc.0 ldarg.2 call void '<msil>fnadd'(native int, native int)
  ldloc.0 ret
}
.method assembly static native int '<msil>ucothunk'(native int pub, native int mgd) cil managed
{
  .maxstack 2
  ldarg.0 ldarg.1 call void '<msil>fnadd'(native int, native int)
  ldarg.0 ret
}
.method assembly static void '<msil>stats'() cil managed
{
  .maxstack 4
  call class [mscorlib]System.IO.TextWriter [mscorlib]System.Console::get_Error()
  ldstr "indirect calls: {0} through the table, {1} native"
  ldsfld int32 '<msil>hits' box [mscorlib]System.Int32 ldsfld int32 '<msil>misses' box [mscorlib]System.Int32
  callvirt instance void [mscorlib]System.IO.TextWriter::WriteLine(string, object, object)
  ret
}
""".replace('NFUNCS', str(max(1, len(funcs)))))

# One dispatcher per calli signature: the table decides between a managed and an unmanaged calli.
calli_sigs = {}
def calli_repl(m):
    ret, params = m.group(1), m.group(2)
    j = calli_sigs.setdefault((ret, params), len(calli_sigs))
    allp = (params + ', native int') if params.strip() else 'native int'
    return f"call {ret} '<msil>icall_{j}'({allp})"
il = re.sub(r"calli (\S+(?: \S+)?)\(([^)]*)\)", calli_repl, il)
for (ret, params), j in calli_sigs.items():
    ps = [p.strip() for p in params.split(',')] if params.strip() else []
    n = len(ps)
    loads = ' '.join(f'ldarg.s {i}' for i in range(n))
    extra.append(f""".method assembly static {ret} '<msil>icall_{j}'({', '.join(ps + ['native int'])}) cil managed aggressiveinlining
{{
  .maxstack {n + 2}
  .locals (native int m)
  ldarg.s {n} call native int '<msil>fnmgd'(native int) dup stloc.0 brfalse.s NAT
  ldsfld int32 '<msil>hits' ldc.i4.1 add stsfld int32 '<msil>hits'
  {loads} ldloc.0 calli {ret}({params}) ret
NAT: ldsfld int32 '<msil>misses' ldc.i4.1 add stsfld int32 '<msil>misses'
  {loads} ldarg.s {n} calli unmanaged cdecl {ret}({params}) ret
}}""")

# The module initializer: native-callable values instead of ldftn values.
for idx, (ret, name, params) in enumerate(funcs):
    old = f"  ldftn {ret} '{name}'({params})\n  stsfld native int '<msil>fn_{name}'"
    if mode == 'delegate':
        j = sigs[(ret, params)]
        new = (f"  ldnull\n  ldftn {ret} '{name}'({params})\n  newobj instance void '<msil>d_{j}'::.ctor(object, native int)\n"
               f"  ldc.i4 {idx}\n  ldftn {ret} '{name}'({params})\n"
               f"  call native int '<msil>mkthunk'(class [mscorlib]System.Delegate, int32, native int)\n"
               f"  stsfld native int '<msil>fn_{name}'")
    else:
        new = (f"  ldftn {ret} '<msil>uco_{name}'({params})\n  ldftn {ret} '{name}'({params})\n"
               f"  call native int '<msil>ucothunk'(native int, native int)\n  stsfld native int '<msil>fn_{name}'")
    assert old in il, old
    il = il.replace(old, new)

# Statistics after main returns (to stderr, so stdout stays the native transcript).
il = re.sub(r"(  call int32 'main'\([^)]*\)\n)", r"\1  call void '<msil>stats'()\n", il, count=1)
il = il.replace('.maxstack 3\n  ldc.i4.s 103', '.maxstack 6\n  ldc.i4.s 103')
il = re.sub(r"(\.method private hidebysig specialname rtspecialname static void \.cctor\(\) cil managed\n\{\n  \.maxstack )(\d+)",
            lambda m: m.group(1) + str(max(6, int(m.group(2)))), il)
il += '\n' + '\n'.join(extra) + '\n'
open(dst, 'w', encoding='utf-8').write(il)
print(f'{len(funcs)} address-taken functions, {len(sigs)} thunk signatures, {len(calli_sigs)} calli signatures')
