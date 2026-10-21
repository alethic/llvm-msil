.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'lib4' { .ver 1:0:0:0 }
.assembly 'prog4' { .ver 0:0:0:0 }

.method public static void say(string) cil managed { .maxstack 1 ldarg.0 call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static void sayi(string, int64) cil managed
{ .maxstack 2 ldarg.0 ldarg.1 box [mscorlib]System.Int64 call string [mscorlib]System.String::Concat(object, object) call void [mscorlib]System.Console::WriteLine(string) ret }

.method public static void s_pure() cil managed noinlining
{ .maxstack 2 ldstr "  s_pure entered" call void say(string)
  ldstr "  pure() = " call int32 [lib4]L.Native::'pure'() conv.i8 call void sayi(string, int64) ret }
.method public static void s_data() cil managed noinlining
{ .maxstack 2 ldstr "  s_data entered" call void say(string)
  ldstr "  data() = " call int32 [lib4]L.Native::'data'() conv.i8 call void sayi(string, int64) ret }
.method public static void s_ldftn() cil managed noinlining
{ .maxstack 2 .locals (native int)
  ldstr "  s_ldftn entered" call void say(string)
  ldftn void [lib4]L.Native::'pure_print'() stloc.0
  ldstr "  after ldftn" call void say(string)
  ldloc.0 calli void()
  ldstr "  after calli" call void say(string) ret }
.method public static void s_field() cil managed noinlining
{ .maxstack 3 ldstr "  s_field entered" call void say(string)
  ldstr "  pub_g != 0: " ldsfld native int [lib4]L.Native::'pub_g' ldc.i4.0 conv.i cgt.un conv.i8 call void sayi(string, int64)
  ldstr "  after field read" call void say(string) ret }
.method public static void s_typeof() cil managed noinlining
{ .maxstack 2 ldstr "  s_typeof entered" call void say(string)
  ldtoken [lib4]L.Native call class [mscorlib]System.Type [mscorlib]System.Type::GetTypeFromHandle(valuetype [mscorlib]System.RuntimeTypeHandle)
  callvirt instance string [mscorlib]System.Type::get_FullName() call void say(string)
  ldstr "  after typeof" call void say(string) ret }
.method public static void s_load() cil managed noinlining
{ .maxstack 2 ldstr "  s_load entered" call void say(string)
  ldstr "lib4" call class [mscorlib]System.Reflection.Assembly [mscorlib]System.Reflection.Assembly::Load(string)
  callvirt instance string [mscorlib]System.Reflection.Assembly::get_FullName() call void say(string)
  ldstr "  after Assembly.Load" call void say(string) ret }
.method public static void s_cold(bool) cil managed noinlining
{ .maxstack 2 ldstr "  s_cold entered" call void say(string)
  ldarg.0 brfalse L1
  call void [lib4]L.Native::'pure_print'()
L1: ldstr "  s_cold: call not taken" call void say(string) ret }
.method public static void s_reflect() cil managed noinlining
{ .maxstack 3 .locals (class [mscorlib]System.Type)
  ldstr "  s_reflect entered" call void say(string)
  ldstr "lib4" call class [mscorlib]System.Reflection.Assembly [mscorlib]System.Reflection.Assembly::Load(string)
  ldstr "L.Native" callvirt instance class [mscorlib]System.Type [mscorlib]System.Reflection.Assembly::GetType(string) stloc.0
  ldstr "  after GetType" call void say(string)
  ldloc.0 ldstr "pure" callvirt instance class [mscorlib]System.Reflection.MethodInfo [mscorlib]System.Type::GetMethod(string) pop
  ldstr "  after GetMethod" call void say(string)
  ldloc.0 ldstr "pure" callvirt instance class [mscorlib]System.Reflection.MethodInfo [mscorlib]System.Type::GetMethod(string)
  ldnull ldnull callvirt instance object [mscorlib]System.Reflection.MethodBase::Invoke(object, object[]) pop
  ldstr "  after Invoke" call void say(string) ret }
.method public static void s_none() cil managed noinlining
{ .maxstack 1 ldstr "  s_none entered (no reference to lib4)" call void say(string) ret }

.method public static int32 main(string[] args) cil managed
{
  .entrypoint
  .maxstack 3
  .locals init (string)
  ldstr "  main entered" call void say(string)
  ldarg.0 ldc.i4.0 ldelem.ref stloc.0
  ldloc.0 ldstr "pure"  call bool [mscorlib]System.String::op_Equality(string, string) brfalse N1 call void s_pure()  br DONE
N1: ldloc.0 ldstr "data"  call bool [mscorlib]System.String::op_Equality(string, string) brfalse N2 call void s_data()  br DONE
N2: ldloc.0 ldstr "ldftn" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N3 call void s_ldftn() br DONE
N3: ldloc.0 ldstr "field" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N4 call void s_field() br DONE
N4: ldloc.0 ldstr "typeof" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N5 call void s_typeof() br DONE
N5: ldloc.0 ldstr "load"  call bool [mscorlib]System.String::op_Equality(string, string) brfalse N6 call void s_load()  br DONE
N6: ldloc.0 ldstr "cold"  call bool [mscorlib]System.String::op_Equality(string, string) brfalse N7 ldc.i4.0 call void s_cold(bool) br DONE
N7: ldloc.0 ldstr "reflect" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N8 call void s_reflect() br DONE
N8: call void s_none()
DONE:
  ldstr "  main done" call void say(string)
  ldc.i4.0
  ret
}
