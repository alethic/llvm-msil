// e09c_lib: does <Module>::.cctor run before an export method that touches NO <Module> member?
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e09c_lib {}
.module e09c_lib.dll
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{ .maxstack 1  ldstr "lib: <Module>::.cctor ran"  call void [mscorlib]System.Console::WriteLine(string)  ret }
.class public abstract sealed auto ansi Exports extends [mscorlib]System.Object
{
  .method public static int32 'pure'(int32 x) cil managed
  { .maxstack 2  ldstr "lib: Exports::pure entered"  call void [mscorlib]System.Console::WriteLine(string)  ldarg.0  ldc.i4.1  add  ret }
}
