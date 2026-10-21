# E4b: two libraries whose data refer to each other (A.pb = &b, B.pa = &a).
# v1: Phase-1 layout, the address fields live in <Module> and <Module>::.cctor
#     allocates the segment and applies the relocations.
# v2: the segment and its address fields live in a class '<msil>Data' whose own
#     .cctor only allocates (no references to other assemblies); <Module>::.cctor
#     applies the relocations (cross-assembly accessors) and runs constructors.
import sys
HDR = '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n'
def lib(me, other, v):
    M, O = me.upper(), other.upper()
    s = HDR + f".assembly extern '{other}' {{ .ver 1:0:0:0 }}\n.assembly '{me}' {{ .ver 1:0:0:0 }}\n"
    say = 'call void [mscorlib]System.Console::WriteLine(string)'
    if v == 1:
        fld = f"'{me}_var'"            # address of this library's variable (in <Module>)
        alloc = f"""  ldc.i4 16 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  dup ldc.i4 {0x1000 if me=='a' else 0x2000} stind.i4
  stsfld native int {fld}
"""
        s += f".field public static initonly native int {fld}\n"
        dataref = f"ldsfld native int {fld}"
        cctor_alloc = alloc
    else:
        s += f""".class private abstract sealed auto ansi beforefieldinit '<msil>Data' extends [mscorlib]System.Object
{{
  .field assembly static initonly native int 'var'
  .method private hidebysig specialname rtspecialname static void .cctor() cil managed
  {{
    .maxstack 3
    ldstr "  {M}: data segment allocated" {say}
    ldc.i4 16 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    dup ldc.i4 {0x1000 if me=='a' else 0x2000} stind.i4
    stsfld native int '<msil>Data'::'var'
    ret
  }}
}}
"""
        dataref = "ldsfld native int '<msil>Data'::'var'"
        cctor_alloc = ""
    s += f""".class public abstract sealed auto ansi '{M}.Native' extends [mscorlib]System.Object
{{
  .method public static native int 'addr'() cil managed {{ .maxstack 1 {dataref} ret }}
  // the C relocation: *(var + 8) = &other_var, applied in <Module>::.cctor
  .method public static native int 'peer'() cil managed {{ .maxstack 2 {dataref} ldc.i4.8 add ldind.i ret }}
}}
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{
  .maxstack 3
  ldstr "  {M}: module initializer begins" {say}
{cctor_alloc}  {dataref} ldc.i4.8 add
  call native int [{other}]{O}.Native::'addr'()
  stind.i
  ldstr "  {M}: module initializer ends" {say}
  ret
}}
"""
    return s
prog = HDR + """.assembly extern 'a' { .ver 1:0:0:0 }
.assembly extern 'b' { .ver 1:0:0:0 }
.assembly 'prog' { .ver 0:0:0:0 }
.method public static void check(string, native int, native int) cil managed
{ .maxstack 3
  ldarg.0 call void [mscorlib]System.Console::Write(string)
  ldarg.1 ldarg.2 ceq brtrue OK
  ldstr " WRONG (stale null address)" call void [mscorlib]System.Console::WriteLine(string) ret
OK: ldstr " ok" call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static void run() cil managed noinlining
{ .maxstack 3
  ldstr "  A.peer == &b:" call native int [a]A.Native::'peer'() call native int [b]B.Native::'addr'() call void check(string, native int, native int)
  ldstr "  B.peer == &a:" call native int [b]B.Native::'peer'() call native int [a]A.Native::'addr'() call void check(string, native int, native int)
  ret }
.method public static int32 main() cil managed { .entrypoint .maxstack 1
  ldstr "  main entered" call void [mscorlib]System.Console::WriteLine(string)
  call void run() ldc.i4.0 ret }
"""
v = int(sys.argv[1])
open('a.il','w').write(lib('a','b',v)); open('b.il','w').write(lib('b','a',v)); open('prog.il','w').write(prog)
