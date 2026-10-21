.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly u { .ver 0:0:0:0 }
.method public hidebysig static pinvokeimpl("msil.unresolved" cdecl) int32 'add3'(int32) cil managed preservesig {}
.method private static int32 viaStub() cil managed { .maxstack 1 ldc.i4.1 call int32 'add3'(int32) ret }
.method private static int32 viaMemberRef() cil managed { .maxstack 1 ldc.i4.1 call int32 [msil.extern]'<msil>Symbols'::'twice'(int32) ret }
.method private static int32 Main(string[] a) cil managed
{
  .entrypoint
  .maxstack 2
  .try { call int32 viaStub() pop leave.s L1 } catch [mscorlib]System.Exception { callvirt instance string [mscorlib]System.Object::ToString() call void [mscorlib]System.Console::WriteLine(string) leave.s L1 }
L1:
  .try { call int32 viaMemberRef() pop leave.s L2 } catch [mscorlib]System.Exception { callvirt instance string [mscorlib]System.Object::ToString() call void [mscorlib]System.Console::WriteLine(string) leave.s L2 }
L2:
  ldc.i4.0 ret
}
