// A stand-in for both clang and the native program it builds, like
// fake-native-44.il, for ../limits-redefine.test: assembled with 'ilasm -exe'
// together with fake-native-cc.il, it is that compiler when it gets
// arguments, and without arguments (the native oracle run) it sleeps for
// four seconds and then exits with status 44, so a native run under a
// timeout of one second does not finish and one under the default does.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'fake-native-sleep' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldarg.0
  ldlen
  conv.i4
  brtrue @BB0_2
  ldc.i4 4000
  call void [mscorlib]System.Threading.Thread::Sleep(int32)
  ldc.i4 44
  ret
@BB0_2:
  ldarg.0
  call int32 '<fake>compile'(string[])
  ret
} // end of method '<msil>Main'
