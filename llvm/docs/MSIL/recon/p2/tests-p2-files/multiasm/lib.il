.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'mylib' { .ver 0:0:0:0 }
.class public abstract sealed 'Contoso.Native' extends [mscorlib]System.Object
{
  .method public static int32 'add'(int32, int32) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    add
    ret
  }
}
