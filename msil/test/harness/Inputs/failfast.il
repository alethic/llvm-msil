// The program of ../loud-failfast.test (plan 5.17.4): a loud failure that
// fails fast, as a trap thunk does, with the toolchain's message. Assembled
// as a DLL for the modern runtimes and as an EXE for the .NET Framework and
// the native run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'failfast' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldstr "msil: trap: the construct needs M99 (loud-failfast)"
  call void [mscorlib]System.Environment::FailFast(string)
  ldc.i4 7
  ret
}
