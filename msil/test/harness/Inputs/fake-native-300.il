// A stand-in for both clang and the native program it builds, so that
// exit-range.test can drive update-expected.py without a built clang.
// Assembled with 'ilasm -exe' together with fake-native-cc.il, it runs
// directly on the .NET Framework, like a native executable. Run with
// arguments (update-expected.py's compile step), it is the compiler of
// fake-native-cc.il: it checks the oracle command line and copies itself to
// the path that follows '-o'. Run without arguments (the native oracle run),
// it exits with status 300 (outside 0..255, plan D37).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'fake-native-300' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldarg.0
  ldlen
  conv.i4
  brtrue @BB0_2
  ldc.i4 300
  ret
@BB0_2:
  ldarg.0
  call int32 '<fake>compile'(string[])
  ret
} // end of method '<msil>Main'
