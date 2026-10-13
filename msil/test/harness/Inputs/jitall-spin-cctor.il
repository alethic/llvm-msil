// A module initializer that does not finish, standing in for a JIT that
// hangs: <Module>::.cctor spins on Environment::get_TickCount. Force-JITting
// any method of <Module> runs the initializer first, so only the timeout of
// msil-jitall.py can stop it. The loop gives up by itself after 120 s, so a
// harness that waits instead of killing (for example a WSL process that
// outlives wsl.exe) does not spin forever; nothing here runs 'main'.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'jitall-spin-cctor' { .ver 0:0:0:0 }

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
  call int32 [mscorlib]System.Environment::get_TickCount()
  stloc.0
@BB0_1:
  call int32 [mscorlib]System.Environment::get_TickCount()
  ldloc.0
  sub
  ldc.i4 120000
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
