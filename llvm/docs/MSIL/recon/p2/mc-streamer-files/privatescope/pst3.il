.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'pst3' { .ver 0:0:0:0 }
// Two C 'static int helper(int)' from two objects, merged into one <Module>:
// privatescope members are exempt from the unique name+signature rule.
.method privatescope static int32 'helper$PST06000001'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.s 10
  add
  ret
}
.method privatescope static int32 'helper$PST06000002'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.s 20
  add
  ret
}
.method private hidebysig static int32 'main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 2
  ldc.i4.1
  call int32 'helper$PST06000001'(int32)
  ldc.i4.2
  call int32 'helper$PST06000002'(int32)
  add
  ret
}
