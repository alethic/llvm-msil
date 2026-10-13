// e02: entrypoint taking string[]; returns int32.Parse(args[0]) as exit code.
// Also prints Environment.Version (via mscorlib type-forward) and argv.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e02_exit {}
.module e02_exit.dll

.method public static int32 '$entry'(string[] args) cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "Environment.Version="
  call void [mscorlib]System.Console::Write(string)
  call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version()
  callvirt instance string [mscorlib]System.Object::ToString()
  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "args.Length="
  call void [mscorlib]System.Console::Write(string)
  ldarg.0
  ldlen
  conv.i4
  call void [mscorlib]System.Console::WriteLine(int32)
  ldstr "GetCommandLineArgs()[0]="
  call void [mscorlib]System.Console::Write(string)
  call string[] [mscorlib]System.Environment::GetCommandLineArgs()
  ldc.i4.0
  ldelem.ref
  call void [mscorlib]System.Console::WriteLine(string)
  ldarg.0
  ldlen
  brfalse.s NOARGS
  ldarg.0
  ldc.i4.0
  ldelem.ref
  call int32 [mscorlib]System.Int32::Parse(string)
  ret
NOARGS:
  ldc.i4.0
  ret
}
