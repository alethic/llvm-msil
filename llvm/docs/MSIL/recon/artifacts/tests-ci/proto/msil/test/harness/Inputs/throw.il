// What a trap lowers to in M1 (ldnull; throw) must show up as an exception.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly 'throw' { }  // 'throw' is an ILAsm keyword: names must be quoted
.method public static int32 Main(string[] args) cil managed {
  .entrypoint
  .maxstack 1
  ldnull
  throw
}
