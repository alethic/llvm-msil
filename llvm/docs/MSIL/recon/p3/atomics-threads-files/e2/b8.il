// E2b: do 8/16-bit Interlocked overloads and Interlocked.And/Or resolve through [mscorlib] on each runtime?
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly b8 { }
.module b8.exe
.class public auto ansi abstract sealed T extends [mscorlib]System.Object {
  .method public static int32 cas8(native int p) cil managed noinlining {
    ldarg.0 ldc.i4.7 ldc.i4.0 call uint8 [mscorlib]System.Threading.Interlocked::CompareExchange(uint8&, uint8, uint8) ret }
  .method public static int32 xchg16(native int p) cil managed noinlining {
    ldarg.0 ldc.i4.7 call int16 [mscorlib]System.Threading.Interlocked::Exchange(int16&, int16) ret }
  .method public static int32 and32(native int p) cil managed noinlining {
    ldarg.0 ldc.i4.3 call int32 [mscorlib]System.Threading.Interlocked::And(int32&, int32) ret }
  .method public static int32 or64(native int p) cil managed noinlining {
    ldarg.0 ldc.i4.3 conv.i8 call int64 [mscorlib]System.Threading.Interlocked::Or(int64&, int64) conv.i4 ret }
  .method public static void try(string name, native int p, int32 which) cil managed {
    .try {
      ldarg.2 brtrue.s A ldarg.1 call int32 T::cas8(native int) pop br.s OK
    A: ldarg.2 ldc.i4.1 bne.un.s B ldarg.1 call int32 T::xchg16(native int) pop br.s OK
    B: ldarg.2 ldc.i4.2 bne.un.s C ldarg.1 call int32 T::and32(native int) pop br.s OK
    C: ldarg.1 call int32 T::or64(native int) pop
    OK: ldarg.0 ldstr ": resolves" call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
      leave.s E
    } catch [mscorlib]System.MissingMethodException {
      pop ldarg.0 ldstr ": MissingMethodException" call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
      leave.s E
    }
  E: ret }
  .method public static int32 Main() cil managed {
    .entrypoint
    .locals init (native int p)
    ldc.i4 64 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stloc.0
    ldstr "CompareExchange(uint8&)" ldloc.0 ldc.i4.0 call void T::try(string, native int, int32)
    ldstr "Exchange(int16&)" ldloc.0 ldc.i4.1 call void T::try(string, native int, int32)
    ldstr "And(int32&)" ldloc.0 ldc.i4.2 call void T::try(string, native int, int32)
    ldstr "Or(int64&)" ldloc.0 ldc.i4.3 call void T::try(string, native int, int32)
    ldc.i4.0 ret }
}
