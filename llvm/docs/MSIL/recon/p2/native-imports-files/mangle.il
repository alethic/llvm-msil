.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly mangle { .ver 0:0:0:0 }
// GetModuleHandle does not exist; kernel32 exports GetModuleHandleA and GetModuleHandleW.
.method public hidebysig static pinvokeimpl("kernel32.dll" as "GetModuleHandle" cdecl) native int 'a'(native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("kernel32.dll" as "GetModuleHandle" nomangle cdecl) native int 'b'(native int) cil managed preservesig {}
.method public static void t1() cil managed { ldc.i4.0 conv.i call native int 'a'(native int) box [mscorlib]System.IntPtr call void [mscorlib]System.Console::WriteLine(object) ret }
.method public static void t2() cil managed { ldc.i4.0 conv.i call native int 'b'(native int) box [mscorlib]System.IntPtr call void [mscorlib]System.Console::WriteLine(object) ret }
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 2
  .try { ldstr "no nomangle -> " call void [mscorlib]System.Console::Write(string) call void t1() leave.s n1 } catch [mscorlib]System.Exception { callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType() call void [mscorlib]System.Console::WriteLine(object) leave.s n1 }
n1:
  .try { ldstr "nomangle    -> " call void [mscorlib]System.Console::Write(string) call void t2() leave.s n2 } catch [mscorlib]System.Exception { callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType() call void [mscorlib]System.Console::WriteLine(object) leave.s n2 }
n2:
  ldc.i4.0
  ret
}
