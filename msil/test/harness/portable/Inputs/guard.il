// Run by ../x86-runner.test: the width and byte-order guard of plan 5.18.7
// (T-P10) at the top of <Module>::.cctor, hand-written in the shape that
// llvm/lib/CLI/TrailerGuard.cpp emits, followed by an entry point that exits
// with 42. In a 64-bit little-endian process the guard falls through; in a
// 32-bit process 'conv.i' of -1 sign-extends only to 32 bits, so the width
// test throws PlatformNotSupportedException with the first T-P10 text from
// the module initializer, before the entry point runs.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'guard' { .ver 0:0:0:0 }

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 2
  ldc.i4.m1
  conv.i
  conv.u8
  ldc.i8 -1
  bne.un FAIL32
  ldc.i4.1
  call uint8[] [mscorlib]System.BitConverter::GetBytes(int32)
  ldc.i4.0
  ldelem.u1
  brfalse FAILBE
  ret
FAIL32:
  ldstr "guard is built for msil64-unknown-dotnet and needs a 64-bit .NET process; this process is 32-bit"
  newobj instance void [mscorlib]System.PlatformNotSupportedException::.ctor(string)
  throw
FAILBE:
  ldstr "guard is built for msil64-unknown-dotnet and needs a little-endian .NET process"
  newobj instance void [mscorlib]System.PlatformNotSupportedException::.ctor(string)
  throw
}

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.s 42
  ret
} // end of method '<msil>Main'
