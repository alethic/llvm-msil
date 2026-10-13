// e01: minimal module. Global (<Module>) static entrypoint in a DLL,
// referencing mscorlib only, int32 return = process exit code.
.assembly extern mscorlib
{
  .publickeytoken = (B7 7A 5C 56 19 34 E0 89)
  .ver 4:0:0:0
}
.assembly e01_hello { .ver 0:0:0:0 }
.module e01_hello.dll

.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "hello from <Module>::main"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.s 42
  ret
}
