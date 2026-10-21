// mscorlib for the Phase-1 members; one method binds NativeMemory to
// System.Runtime.InteropServices 8.0.0.0 and is called only when argc > 1.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly 'v17-lightup' { .ver 0:0:0:0 }
.method public static native int alloc_modern(native uint) cil managed noinlining
{
  .maxstack 1
  ldarg.0
  call void* [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Alloc(native uint)
  ret
}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 2
  call string[] [mscorlib]System.Environment::GetCommandLineArgs()
  ldlen
  conv.i4
  ldc.i4.1
  ble FALLBACK
  ldc.i4.s 16
  conv.u
  call native int alloc_modern(native uint)
  pop
  ldc.i4.s 43
  ret
FALLBACK:
  ldc.i4.s 16
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  pop
  ldc.i4.s 42
  ret
}
