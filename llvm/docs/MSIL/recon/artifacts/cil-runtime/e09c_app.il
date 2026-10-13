// e09c_app: consumer assembly
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly extern e09c_lib { .ver 0:0:0:0 }
.assembly e09c_app {}
.module e09c_app.dll
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 2
  ldstr "app: main entered"  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4 41
  call int32 [e09c_lib]Exports::'pure'(int32)
  ret
}
