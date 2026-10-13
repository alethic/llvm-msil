// e01c: reference the modern contract assemblies instead of mscorlib
.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly extern System.Console { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly e01c_sysruntime {}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "System.Runtime/System.Console refs"
  call void [System.Console]System.Console::WriteLine(string)
  ldc.i4.s 42
  ret
}
