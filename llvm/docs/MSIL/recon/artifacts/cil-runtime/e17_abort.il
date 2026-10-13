// e17: abnormal termination paths and their process exit codes.
//   arg "throw"    : unhandled managed exception (what 'unreachable'/'trap' would do if lowered to throw)
//   arg "failfast" : Environment.FailFast
//   arg "abort"    : native abort() via P/Invoke
//   arg "void"     : entrypoint returning void
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e17_abort {}
.module e17_abort.dll
.method public static pinvokeimpl("ucrtbase.dll" cdecl) void 'abort'() cil managed preservesig {}
.method public static int32 'main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 2
  ldarg.0  ldc.i4.0  ldelem.ref  ldstr "throw"     call bool [mscorlib]System.String::op_Equality(string, string)  brfalse.s N1
  ldstr "trap"  newobj instance void [mscorlib]System.InvalidOperationException::.ctor(string)  throw
N1:
  ldarg.0  ldc.i4.0  ldelem.ref  ldstr "failfast"  call bool [mscorlib]System.String::op_Equality(string, string)  brfalse.s N2
  ldstr "trap"  call void [mscorlib]System.Environment::FailFast(string)
N2:
  ldarg.0  ldc.i4.0  ldelem.ref  ldstr "abort"     call bool [mscorlib]System.String::op_Equality(string, string)  brfalse.s N3
  call void 'abort'()
N3:
  ldc.i4.5
  ret
}
