.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'lib2' { .ver 1:0:0:0 }
.assembly 'prog2' { .ver 0:0:0:0 }

// C: extern int counter; int (*fp)(int) = twice;  the program's own address fields
.field assembly static initonly native int 'counter'
.field assembly static initonly native int '<msil>fn_twice'          // from ldftn on the MemberRef
.field assembly static initonly native int '<msil>fn_twice_lib'      // from the library's accessor
.field assembly static native int 'P2'

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 1
  call native int [lib2]'Contoso.Lib2.Native'::'<msil>addr_counter'()
  stsfld native int 'counter'
  ldftn int32 [lib2]'Contoso.Lib2.Native'::'twice'(int32)
  stsfld native int '<msil>fn_twice'
  call native int [lib2]'Contoso.Lib2.Native'::'<msil>addr_twice'()
  stsfld native int '<msil>fn_twice_lib'
  ret
}
.method public static void cmp(string, native int, native int) cil managed
{ .maxstack 3 ldarg.0 call void [mscorlib]System.Console::Write(string)
  ldarg.1 ldarg.2 ceq brtrue EQ ldstr " DIFFER" call void [mscorlib]System.Console::WriteLine(string) ret
EQ: ldstr " equal" call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static void sayi(string, int32) cil managed
{ .maxstack 2 ldarg.0 ldarg.1 box [mscorlib]System.Int32 call string [mscorlib]System.String::Concat(object, object) call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static void late_ldftn() cil managed noinlining
{ .maxstack 1 ldftn int32 [lib2]'Contoso.Lib2.Native'::'twice'(int32) stsfld native int 'P2' ret }

.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 4
  .locals (int32 i)
  // run 'twice' enough times to have it compiled (and tiered up on CoreCLR)
  ldc.i4.0 stloc.0
LOOP: ldloc.0 ldsfld native int '<msil>fn_twice' calli int32(int32) pop
  ldloc.0 ldc.i4.1 add dup stloc.0 ldc.i4 200000 blt LOOP
  ldc.i4 200 call void [mscorlib]System.Threading.Thread::Sleep(int32)
  call void late_ldftn()
  ldstr "  &twice: program ldftn (.cctor) vs library '<msil>fn_twice':" ldsfld native int '<msil>fn_twice' call native int [lib2]'Contoso.Lib2.Native'::'get_twice'() call void cmp(string, native int, native int)
  ldstr "  &twice: program ldftn (late) vs library '<msil>fn_twice':" ldsfld native int 'P2' call native int [lib2]'Contoso.Lib2.Native'::'get_twice'() call void cmp(string, native int, native int)
  ldstr "  &twice: library same-module ldftn (late) vs library '<msil>fn_twice':" call native int [lib2]'Contoso.Lib2.Native'::'ldftn_late'() call native int [lib2]'Contoso.Lib2.Native'::'get_twice'() call void cmp(string, native int, native int)
  ldstr "  &twice: library accessor vs library get_twice():" ldsfld native int '<msil>fn_twice_lib' call native int [lib2]'Contoso.Lib2.Native'::'get_twice'() call void cmp(string, native int, native int)
  // calls through pointers
  ldstr "  calli program pointer: twice(21) = " ldc.i4.s 21 ldsfld native int '<msil>fn_twice' calli int32(int32) call void sayi(string, int32)
  ldstr "  calli library pointer: twice(21) = " ldc.i4.s 21 call native int [lib2]'Contoso.Lib2.Native'::'get_twice'() calli int32(int32) call void sayi(string, int32)
  // extern data through the program's own address field
  ldsfld native int 'counter' ldc.i4 40 stind.i4
  ldstr "  bump() after the program stored 40 = " call int32 [lib2]'Contoso.Lib2.Native'::'bump'() call void sayi(string, int32)
  ldstr "  &counter: program field vs exported field:" ldsfld native int 'counter' ldsfld native int [lib2]'Contoso.Lib2.Native'::'counter' call void cmp(string, native int, native int)
  ldc.i4.0
  ret
}
