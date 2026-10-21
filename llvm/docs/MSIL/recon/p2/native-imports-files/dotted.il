.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly dotted { .ver 0:0:0:0 }
.method public hidebysig static pinvokeimpl("my.dot" as "fn1" cdecl) int32 'a'(int32) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("my.dot.dll" as "fn1" cdecl) int32 'b'(int32) cil managed preservesig {}
.method public static void t1() cil managed { ldc.i4.1 call int32 'a'(int32) call void [mscorlib]System.Console::WriteLine(int32) ret }
.method public static void t2() cil managed { ldc.i4.1 call int32 'b'(int32) call void [mscorlib]System.Console::WriteLine(int32) ret }
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 2
  .try { ldstr "my.dot     -> " call void [mscorlib]System.Console::Write(string) call void t1() leave.s n1 } catch [mscorlib]System.Exception { callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType() call void [mscorlib]System.Console::WriteLine(object) leave.s n1 }
n1:
  .try { ldstr "my.dot.dll -> " call void [mscorlib]System.Console::Write(string) call void t2() leave.s n2 } catch [mscorlib]System.Exception { callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType() call void [mscorlib]System.Console::WriteLine(object) leave.s n2 }
n2:
  ldc.i4.0
  ret
}
