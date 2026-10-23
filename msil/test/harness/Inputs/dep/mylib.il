// The managed dependency of prog.il (../../dep-local.test, dep-wsl.test): a
// library assembly 'mylib' with one public static method, as a C# library or
// a later msil-ld library would provide it (recon p2/tests-p2 2.5, E5).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'mylib' { .ver 0:0:0:0 }

.class public abstract auto ansi sealed 'Contoso.Lib' extends [mscorlib]System.Object
{
  .method public static int32 'add'(int32 a, int32 b) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    add
    ret
  } // end of method 'add'
}
