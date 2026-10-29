// E6b: does the JIT hoist a plain ldind out of a spin loop? (relaxed atomic load lowered without volatile.)
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly hoist { }
.module hoist.exe
.class public auto ansi abstract sealed H extends [mscorlib]System.Object {
  .field static native int p
  .method public static void spin_plain() cil managed noinlining {
    .locals init (native int q)
    ldsfld native int H::p stloc.0
  L: ldloc.0 ldind.i4 brfalse.s L
    ret }
  .method public static void spin_volatile() cil managed noinlining {
    .locals init (native int q)
    ldsfld native int H::p stloc.0
  L: ldloc.0 volatile. ldind.i4 brfalse.s L
    ret }
  .method public static void spin_count() cil managed noinlining {
    // plain load with a counter in the loop (loop has other work)
    .locals init (native int q, int32 n)
    ldsfld native int H::p stloc.0
  L: ldloc.1 ldc.i4.1 add stloc.1 ldloc.0 ldind.i4 brfalse.s L
    ret }
  .method public static string run(string which) cil managed {
    .locals init (class [mscorlib]System.Threading.Thread t)
    ldsfld native int H::p ldc.i4.0 stind.i4
    ldarg.0 ldstr "plain" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s V
    ldnull ldftn void H::spin_plain() br.s MK
  V: ldarg.0 ldstr "volatile" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s C
    ldnull ldftn void H::spin_volatile() br.s MK
  C: ldnull ldftn void H::spin_count()
  MK: newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
    newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart)
    stloc.0
    ldloc.0 ldc.i4.1 callvirt instance void [mscorlib]System.Threading.Thread::set_IsBackground(bool)
    ldloc.0 callvirt instance void [mscorlib]System.Threading.Thread::Start()
    ldc.i4 300 call void [mscorlib]System.Threading.Thread::Sleep(int32)
    ldsfld native int H::p ldc.i4.1 volatile. stind.i4
    ldloc.0 ldc.i4 3000 callvirt instance bool [mscorlib]System.Threading.Thread::Join(int32)
    brtrue.s OK
    ldstr "HUNG (load hoisted)" ret
  OK: ldstr "exited" ret }
  .method public static int32 Main(string[] a) cil managed {
    .entrypoint
    ldc.i4.s 16 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stsfld native int H::p
    ldstr "plain: " ldstr "plain" call string H::run(string) call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    ldstr "plain+counter: " ldstr "count" call string H::run(string) call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    ldstr "volatile: " ldstr "volatile" call string H::run(string) call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.0 ret }
}
