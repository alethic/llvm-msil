"""E3b: startup cost of publishing every exported function's address eagerly.

lib-<mode>.il has N exported 'void f_i()' methods and a <Module>::.cctor that, per mode:
  none      does nothing per function
  ldftn     stores ldftn of each export in a public initonly field (P2: managed pointers)
  delegate  creates an Action delegate per export, keeps it alive, and stores
            Marshal.GetFunctionPointerForDelegate (the P3 .NET Framework thunk)
prog.il times the first call into the library (which runs the module initializer).
"""
import sys
N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
H = ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n"
for mode in ("none", "ldftn", "delegate"):
    o = [H, ".assembly 'tlib' { .ver 1:0:0:0 }",
         ".class public abstract sealed auto ansi 'T.Native' extends [mscorlib]System.Object\n{"]
    for i in range(N):
        o.append(f"  .method public static void 'f_{i}'() cil managed {{ .maxstack 1 ret }}")
        if mode != "none":
            o.append(f"  .field public static initonly native int '<msil>fn_f_{i}'")
    o.append("}")
    o.append(".field assembly static class [mscorlib]System.Delegate[] '<msil>keep'")
    o.append(".method private hidebysig specialname rtspecialname static void .cctor() cil managed\n{\n  .maxstack 4")
    if mode == "delegate":
        o.append(f"  ldc.i4 {N} newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] '<msil>keep'")
    for i in range(N):
        if mode == "ldftn":
            o.append(f"  ldftn void 'T.Native'::'f_{i}'() stsfld native int 'T.Native'::'<msil>fn_f_{i}'")
        elif mode == "delegate":
            o.append(f"  ldsfld class [mscorlib]System.Delegate[] '<msil>keep' ldc.i4 {i} ldnull ldftn void 'T.Native'::'f_{i}'()"
                     f" newobj instance void [mscorlib]System.Action::.ctor(object, native int) dup"
                     f" call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate)"
                     f" stsfld native int 'T.Native'::'<msil>fn_f_{i}' stelem.ref")
    o.append("  ret\n}")
    open(f"tlib-{mode}.il", "w").write("\n".join(o) + "\n")
open("tprog.il", "w").write(H + """.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'tlib' { .ver 1:0:0:0 }
.assembly 'tprog' { .ver 0:0:0:0 }
.method public static void first() cil managed noinlining { .maxstack 1 call void [tlib]'T.Native'::'f_0'() ret }
.method public static int32 main() cil managed
{ .entrypoint .maxstack 3 .locals (class [System]System.Diagnostics.Stopwatch)
  call class [System]System.Diagnostics.Stopwatch [System]System.Diagnostics.Stopwatch::StartNew() stloc.0
  call void first()
  ldstr "  first call into the library (load + module initializer): {0:F2} ms"
  ldloc.0 callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() conv.r8 ldc.r8 1000 mul
  ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div box [mscorlib]System.Double
  call string [mscorlib]System.String::Format(string, object) call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.0 ret }
""")
