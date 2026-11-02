// Pure-IL width/endianness guard: no BCL member except the exception's .ctor.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly guardB { .ver 1:0:0:0 }
.module guardB
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 2
  .locals init (int32 probe)
  // width: (uint64)(native int)-1 (conv.i sign-extends, conv.u8 then zero-extends) == UINT64_MAX only when native int has 64 bits
  ldc.i4.m1
  conv.i
  conv.u8
  ldc.i8 -1
  bne.un FAIL
  // byte order: the first byte of the int32 1 is 1 only on a little-endian machine
  ldc.i4.1
  call uint8[] [mscorlib]System.BitConverter::GetBytes(int32)
  ldc.i4.0
  ldelem.u1
  brfalse FAIL
  ret
FAIL:
  ldstr "guardB: this program was compiled for msil64-unknown-dotnet and needs a 64-bit little-endian .NET process; this process is 32-bit or big-endian. Run it with a 64-bit .NET runtime, or rebuild it for msil32-unknown-dotnet."
  newobj instance void [mscorlib]System.PlatformNotSupportedException::.ctor(string)
  throw
}
.method public static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.s 42
  ret
}
