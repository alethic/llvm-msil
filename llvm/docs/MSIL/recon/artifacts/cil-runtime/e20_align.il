// e20: observed alignment of localloc results and Marshal.AllocHGlobal results
// (prints OR of the low 6 address bits over many allocations; 0 => 64-aligned, 8 => only 8-aligned, ...)
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e20_align {}
.method assembly static int32 'll'(int32 n) cil managed noinlining
{ .maxstack 2  ldarg.0 localloc conv.i4 ldc.i4 63 and ret }
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 3
  .locals init (int32 i, int32 accL, int32 accH)
L:
  ldloc.1  ldloc.0 ldc.i4.1 add  call int32 'll'(int32)  or  stloc.1
  ldloc.2  ldloc.0 ldc.i4.1 add conv.i  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)  conv.i4 ldc.i4 63 and  or  stloc.2
  ldloc.0 ldc.i4.1 add dup stloc.0 ldc.i4 200 blt.s L
  ldstr "OR of low 6 bits: localloc="  call void [mscorlib]System.Console::Write(string)
  ldloc.1  call void [mscorlib]System.Console::Write(int32)
  ldstr " AllocHGlobal="  call void [mscorlib]System.Console::Write(string)
  ldloc.2  call void [mscorlib]System.Console::WriteLine(int32)
  ldc.i4.0
  ret
}
