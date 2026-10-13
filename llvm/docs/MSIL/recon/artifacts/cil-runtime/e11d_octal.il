// e11d: non-ASCII identifier spelled with octal escapes of its UTF-8 bytes
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e11d_octal {}
.module e11d_octal.dll
.method assembly static int32 '\303\274n\303\257c\303\266d\303\251'() cil managed { .maxstack 1 ldc.i4 14 ret }
.method public static int32 '__msil_start'() cil managed
{
  .entrypoint
  .maxstack 2
  ldftn int32 '\303\274n\303\257c\303\266d\303\251'()
  pop
  ldstr "ldstr with octal escapes: \303\274"
  call void [mscorlib]System.Console::WriteLine(string)
  call int32 '\303\274n\303\257c\303\266d\303\251'()
  ret
}
