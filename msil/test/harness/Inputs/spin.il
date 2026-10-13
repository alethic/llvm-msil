// A busy loop that stands in for a hung program (or a hung JIT). It gives up
// after 120 seconds so that a runner that fails to kill it (for example a WSL
// process that outlives wsl.exe) does not spin forever; the harness timeout in
// timeout.test is far shorter, so the transcript must be '[timeout]'.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'spin' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
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
  ldc.i4.s 99
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
