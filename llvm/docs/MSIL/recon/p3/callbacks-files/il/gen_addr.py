# gen_addr.py: prints the addresses of six delegate thunks, six ldftn values and (addruco) six UCO ldftn
# values, to show whether native-callable entry points of one assembly fall in one range.
import os
HERE = os.path.dirname(os.path.abspath(__file__))
UCO = ('.custom instance void [System.Runtime.InteropServices]System.Runtime.InteropServices.'
       'UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )')


def gen(name, uco):
    o = [f'''.assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
{'.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }' if uco else ''}
.assembly {name} {{ .ver 0:0:0:0 }}
.module {name}.exe
.corflags 0x00000001
.class private auto ansi sealed D_i_i extends [mscorlib]System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {{}}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {{}}
}}
.class private abstract auto ansi sealed Prog extends [mscorlib]System.Object
{{
  .field static class [mscorlib]System.Delegate[] keep
  .method static void p(string s, native int v) cil managed {{ .maxstack 3 ldarg.0 ldarg.1 call int64 [mscorlib]System.IntPtr::op_Explicit(native int) box [mscorlib]System.Int64 call void [mscorlib]System.Console::WriteLine(string, object) ret }}''']
    for k in range(6):
        o.append(f'  .method static int32 f{k}(int32 x) cil managed {{ .maxstack 1 ldarg.0 ret }}')
        if uco:
            o.append(f'  .method static int32 u{k}(int32 x) cil managed {{ {UCO} .maxstack 1 ldarg.0 ret }}')
    o.append('''  .method static int32 Main() cil managed { .entrypoint .maxstack 6
    ldc.i4.8 newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] Prog::keep''')
    for k in range(6):
        o.append(f'''    ldstr "delegate thunk f{k}: {{0:X}}" ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {k} ldnull ldftn int32 Prog::f{k}(int32) newobj instance void D_i_i::.ctor(object, native int) stelem.ref
    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4 {k} ldelem.ref call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) call void Prog::p(string, native int)''')
    for k in range(6):
        o.append(f'    ldstr "ldftn f{k}: {{0:X}}" ldftn int32 Prog::f{k}(int32) call void Prog::p(string, native int)')
        if uco:
            o.append(f'    ldstr "ldftn UCO u{k}: {{0:X}}" ldftn int32 Prog::u{k}(int32) call void Prog::p(string, native int)')
    o.append('    ldc.i4.0 ret } }')
    with open(os.path.join(HERE, f'{name}.il'), 'w') as f:
        f.write('\n'.join(o) + '\n')


gen('addrfx', False)
gen('addruco', True)
