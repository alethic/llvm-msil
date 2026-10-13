// e01b: the old MSILWriter's style: '.assembly extern mscorlib {}' (no version, no public key token)
.assembly extern mscorlib {}
.assembly e01b_bareref {}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "bare mscorlib ref"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.s 42
  ret
}
