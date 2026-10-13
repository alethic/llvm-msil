// A module initializer that commits memory without bound, the way the .NET
// 10 JIT did on an inconsistent stack depth (cil-runtime recon, e15): 4096
// allocations of 1 MiB (4 GiB in total, never touched, so no RAM is used
// without the cap). Force-JITting any method of <Module> runs the
// initializer first, so the memory cap of msil-jitall.py must stop it. It
// imports nothing, so it behaves the same on every OS.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'jitall-hog-cctor' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.0
  ret
} // end of method 'main'

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 2
  .locals (int32)
  ldc.i4.0
  stloc.0
@BB0_1:
  ldc.i4 0x100000
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  pop
  ldloc.0
  ldc.i4.1
  add
  dup
  stloc.0
  ldc.i4 4096
  blt @BB0_1
  ret
} // end of method '.cctor'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
