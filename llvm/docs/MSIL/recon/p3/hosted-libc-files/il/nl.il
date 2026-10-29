// hosted-libc recon: is System.Runtime.InteropServices.NativeLibrary reachable through [mscorlib] on .NET 8/10?
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly nl { }
.module nl.exe
.method static int32 Main() {
  .entrypoint
  .locals init (native int h, native int p)
  ldstr "libc.so.6"
  call native int [mscorlib]System.Runtime.InteropServices.NativeLibrary::Load(string)
  stloc.0
  ldloc.0
  ldstr "stdout"
  call native int [mscorlib]System.Runtime.InteropServices.NativeLibrary::GetExport(native int, string)
  stloc.1
  ldstr "NativeLibrary via [mscorlib]: stdout export found="
  call void [mscorlib]System.Console::Write(string)
  ldloc.1
  ldc.i4.0
  conv.i
  cgt.un
  call void [mscorlib]System.Console::WriteLine(bool)
  ldc.i4.0
  ret
}
