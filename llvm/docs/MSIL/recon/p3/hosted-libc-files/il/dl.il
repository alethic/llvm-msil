// hosted-libc recon: which module serves dlopen on this glibc: libc.so.6 or libdl.so.2?
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly dl { }
.module dl.exe
.method assembly static pinvokeimpl("libc.so.6" as "dlopen" nomangle cdecl) native int dlopen_c(native int, int32) preservesig {}
.method assembly static pinvokeimpl("libdl.so.2" as "dlopen" nomangle cdecl) native int dlopen_dl(native int, int32) preservesig {}
.method static void Try(int32 which) {
  .try {
    ldarg.0
    brtrue.s DL
    ldc.i4.0
    conv.i
    ldc.i4.1
    call native int dlopen_c(native int, int32)
    pop
    ldstr "libc.so.6!dlopen: bound"
    call void [mscorlib]System.Console::WriteLine(string)
    leave.s END
  DL:
    ldc.i4.0
    conv.i
    ldc.i4.1
    call native int dlopen_dl(native int, int32)
    pop
    ldstr "libdl.so.2!dlopen: bound"
    call void [mscorlib]System.Console::WriteLine(string)
    leave.s END
  } catch [mscorlib]System.Exception {
    callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
    callvirt instance string [mscorlib]System.Reflection.MemberInfo::get_Name()
    call void [mscorlib]System.Console::WriteLine(string)
    leave.s END
  }
END:
  ret
}
.method static int32 Main() {
  .entrypoint
  ldc.i4.0
  call void Try(int32)
  ldc.i4.1
  call void Try(int32)
  ldc.i4.0
  ret
}
