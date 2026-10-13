// e08d: negative P/Invoke tests (each in its own try/catch; prints the exception type).
//   1. DisableRuntimeMarshalling really is in effect: a 'string' parameter is rejected.
//   2. P/Invoke WITHOUT 'preservesig' (HRESULT-transform semantics).
//   3. pinvokeimpl("libc") on Windows.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly e08d_negative
{
  .custom instance void [System.Runtime]System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute::.ctor() = (01 00 00 00)
}
.module e08d_negative.dll

.method public static pinvokeimpl("ucrtbase.dll" as "strlen" cdecl) native int 'strlen_str'(string s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'abs'(int32 x) cil managed {}          // no preservesig
.method public static pinvokeimpl("libc" cdecl) int32 'getpid'() cil managed preservesig {}

.method static void 't1'() cil managed { .maxstack 1  ldstr "abc"  call native int 'strlen_str'(string)  pop  ret }
.method static void 't2'() cil managed
{
  .maxstack 2
  ldstr "abs(-5) without preservesig returned "  call void [mscorlib]System.Console::Write(string)
  ldc.i4 -5  call int32 'abs'(int32)  call void [mscorlib]System.Console::WriteLine(int32)
  ret
}
.method static void 't3'() cil managed { .maxstack 1  call int32 'getpid'()  pop  ret }

.method static void 'run'(native int fp, string name) cil managed
{
  .maxstack 2
  .try
  {
    ldarg.0
    calli void()
    ldstr "  OK (no exception): "  ldarg.1  call string [mscorlib]System.String::Concat(string, string)
    call void [mscorlib]System.Console::WriteLine(string)
    leave.s DONE
  }
  catch [mscorlib]System.Exception
  {
    ldstr "  "  call void [mscorlib]System.Console::Write(string)
    ldarg.1  call void [mscorlib]System.Console::Write(string)
    ldstr " -> "  call void [mscorlib]System.Console::Write(string)
    callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
    callvirt instance string [mscorlib]System.Object::ToString()
    call void [mscorlib]System.Console::WriteLine(string)
    leave.s DONE
  }
DONE:
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 2
  ldftn void 't1'()  ldstr "string param under DisableRuntimeMarshalling"  call void 'run'(native int, string)
  ldftn void 't2'()  ldstr "pinvoke without preservesig"  call void 'run'(native int, string)
  ldftn void 't3'()  ldstr "pinvokeimpl(\"libc\") on Windows"  call void 'run'(native int, string)
  ldc.i4.0
  ret
}
