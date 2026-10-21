// E7 bench: cost of reaching imported data through the library's accessor on every access
// (call [zl]'<msil>addr_counter'()) versus a cached import field in the program.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'zl' { .ver 1:0:0:0 }
.assembly 'bench' { .ver 0:0:0:0 }
.field static initonly native int 'imp_counter'
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{ .maxstack 1 call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() stsfld native int 'imp_counter' ret }
.method public static int32 via_accessor(int32) cil managed noinlining
{ .maxstack 3 .locals (int32 s, int32 i)
  ldc.i4.0 stloc.0 ldc.i4.0 stloc.1 br C
L: ldloc.0 call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() ldind.i4 add stloc.0 ldloc.1 ldc.i4.1 add stloc.1
C: ldloc.1 ldarg.0 blt L ldloc.0 ret }
.method public static int32 via_cached(int32) cil managed noinlining
{ .maxstack 3 .locals (int32 s, int32 i)
  ldc.i4.0 stloc.0 ldc.i4.0 stloc.1 br C
L: ldloc.0 ldsfld native int 'imp_counter' ldind.i4 add stloc.0 ldloc.1 ldc.i4.1 add stloc.1
C: ldloc.1 ldarg.0 blt L ldloc.0 ret }
.method public static void time(string, bool) cil managed
{ .maxstack 3 .locals (class [System]System.Diagnostics.Stopwatch, int32)
  call class [System]System.Diagnostics.Stopwatch [System]System.Diagnostics.Stopwatch::StartNew() stloc.0
  ldarg.1 brfalse CACHED ldc.i4 200000000 call int32 via_accessor(int32) stloc.1 br DONE
CACHED: ldc.i4 200000000 call int32 via_cached(int32) stloc.1
DONE:
  ldarg.0 ldloc.0 callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedMilliseconds() box [mscorlib]System.Int64
  call string [mscorlib]System.String::Format(string, object) call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static int32 main() cil managed
{ .entrypoint .maxstack 2 .locals (int32)
  ldc.i4.0 stloc.0
R: ldstr "  accessor per access: {0} ms / 200M" ldc.i4.1 call void time(string, bool)
  ldstr "  cached import field: {0} ms / 200M" ldc.i4.0 call void time(string, bool)
  ldloc.0 ldc.i4.1 add dup stloc.0 ldc.i4.2 blt R
  ldc.i4.0 ret }
