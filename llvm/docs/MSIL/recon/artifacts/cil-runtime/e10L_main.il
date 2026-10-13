// e10: generated startup: Main(string[]) builds C argc/argv (char**, NULL-terminated)
// in unmanaged memory and calls  int main(int argc, char **argv)  ==  int32 'main'(int32, native int).
// The C 'main' below is what the backend would emit for:
//   int main(int argc, char **argv) {
//     for (int i = 0; i < argc; i++) puts(argv[i]);
//     if (argv[argc] != 0) return 100;
//     if (argc > 1 && argv[1][0] == 'x') exit(7);        // native exit() via P/Invoke
//     return argc + 40;
//   }
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e10L_main {}
.module e10L_main.dll

.method public static pinvokeimpl("libc" cdecl) int32 'puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("libc" cdecl) void 'exit'(int32 code) cil managed preservesig {}

// ---- "C" main
.method public static int32 'main'(int32 argc, native int argv) cil managed
{
  .maxstack 3
  .locals init (int32 i)
  ldc.i4.0
  stloc.0
  br.s COND
BODY:
  ldarg.1
  ldloc.0
  conv.i
  ldc.i4.8
  conv.i
  mul
  add
  ldind.i
  call int32 'puts'(native int)
  pop
  ldloc.0
  ldc.i4.1
  add
  stloc.0
COND:
  ldloc.0
  ldarg.0
  blt.s BODY
  // argv[argc] must be NULL
  ldarg.1
  ldarg.0
  conv.i
  ldc.i4.8
  conv.i
  mul
  add
  ldind.i
  brfalse.s NULLOK
  ldc.i4 100
  ret
NULLOK:
  ldarg.0
  ldc.i4.1
  ble.s RET
  ldarg.1
  ldc.i4.8
  add
  ldind.i
  ldind.u1
  ldc.i4 0x78          // 'x'
  bne.un.s RET
  ldc.i4.7
  call void 'exit'(int32)
RET:
  ldarg.0
  ldc.i4 40
  add
  ret
}

// ---- generated startup (what the backend emits when the module defines main)
.method public static int32 '__msil_start'(string[] args) cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (int32 argc, native int argv, int32 i)
  // argc = args.Length + 1   (argv[0] = program path)
  ldarg.0
  ldlen
  conv.i4
  ldc.i4.1
  add
  stloc.0
  // argv = AllocHGlobal((argc + 1) * sizeof(char*))
  ldloc.0
  ldc.i4.1
  add
  conv.i
  sizeof native int
  mul
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc.1
  // argv[0] = program path (Environment.GetCommandLineArgs()[0] is the .dll path under 'dotnet x.dll')
  ldloc.1
  call string[] [mscorlib]System.Environment::GetCommandLineArgs()
  ldc.i4.0
  ldelem.ref
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  stind.i
  // for (i = 0; i < args.Length; i++) argv[i+1] = StringToHGlobalAnsi(args[i])
  ldc.i4.0
  stloc.2
  br.s ACOND
ABODY:
  ldloc.1
  ldloc.2
  ldc.i4.1
  add
  conv.i
  sizeof native int
  mul
  add
  ldarg.0
  ldloc.2
  ldelem.ref
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  stind.i
  ldloc.2
  ldc.i4.1
  add
  stloc.2
ACOND:
  ldloc.2
  ldarg.0
  ldlen
  conv.i4
  blt.s ABODY
  // argv[argc] = NULL
  ldloc.1
  ldloc.0
  conv.i
  sizeof native int
  mul
  add
  ldc.i4.0
  conv.u
  stind.i
  // return main(argc, argv)
  ldloc.0
  ldloc.1
  call int32 'main'(int32, native int)
  ret
}
