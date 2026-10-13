// The compiler half of the fake native toolchain of exit-range.test
// (fake-native-<N>.il), assembled together with each of them. It checks the
// command line that update-expected.py gives the oracle compiler against
// plan M0.b items (6) and (8), as the native-oracle legs of %{msil_c} and
// %{msil_ll} build it:
//   one source file, '-o <exe>' and a native '--target=<triple>' (not an
//   -msil triple);
//   a .c source: -ffreestanding, -Werror=return-type and an -I directory
//   (either '-I <dir>' or '-I<dir>') that holds msil_test.h (%{msil_cflags});
//   a .ll source: -Wno-override-module.
// Other options (-O<level>) are accepted. On a mismatch it prints what is
// wrong and the command line to stderr and exits with 1; otherwise it
// "compiles" by copying itself to <exe>. update-expected.py runs the
// compiler as an executable (msil-run.py's run_limited), so this is an
// 'ilasm -exe' program, which runs directly on the .NET Framework.

.method assembly static bool '<fake>has_header'(string) cil managed
{
  .maxstack 2
  ldarg.0
  ldstr "msil_test.h"
  call string [mscorlib]System.IO.Path::Combine(string, string)
  call bool [mscorlib]System.IO.File::Exists(string)
  ret
} // end of method '<fake>has_header'

.method assembly static int32 '<fake>compile'(string[] args) cil managed
{
  .maxstack 4
  .locals init (int32 i, string arg, string target, string output,
                string source, int32 sources, bool freestanding,
                bool returntype, bool include, bool nooverride,
                string problem)
  ldc.i4.0
  stloc i
@LOOP:
  ldloc i
  ldarg.0
  ldlen
  conv.i4
  bge @DONE
  ldarg.0
  ldloc i
  ldelem.ref
  stloc arg
  // -o <exe>
  ldloc arg
  ldstr "-o"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse @NOT_O
  ldloc i
  ldc.i4.1
  add
  stloc i
  ldloc i
  ldarg.0
  ldlen
  conv.i4
  bge @NO_VALUE
  ldarg.0
  ldloc i
  ldelem.ref
  stloc output
  br @NEXT
@NOT_O:
  // -I <dir>
  ldloc arg
  ldstr "-I"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse @NOT_I_SEPARATE
  ldloc i
  ldc.i4.1
  add
  stloc i
  ldloc i
  ldarg.0
  ldlen
  conv.i4
  bge @NO_VALUE
  ldarg.0
  ldloc i
  ldelem.ref
  call bool '<fake>has_header'(string)
  brfalse @NEXT
  ldc.i4.1
  stloc include
  br @NEXT
@NOT_I_SEPARATE:
  // -I<dir>
  ldloc arg
  ldstr "-I"
  callvirt instance bool [mscorlib]System.String::StartsWith(string)
  brfalse @NOT_I
  ldloc arg
  ldc.i4.2
  callvirt instance string [mscorlib]System.String::Substring(int32)
  call bool '<fake>has_header'(string)
  brfalse @NEXT
  ldc.i4.1
  stloc include
  br @NEXT
@NOT_I:
  ldloc arg
  ldstr "--target="
  callvirt instance bool [mscorlib]System.String::StartsWith(string)
  brfalse @NOT_TARGET
  ldloc arg
  ldc.i4.s 9
  callvirt instance string [mscorlib]System.String::Substring(int32)
  stloc target
  br @NEXT
@NOT_TARGET:
  ldloc arg
  ldstr "-ffreestanding"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse @NOT_FREESTANDING
  ldc.i4.1
  stloc freestanding
  br @NEXT
@NOT_FREESTANDING:
  ldloc arg
  ldstr "-Werror=return-type"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse @NOT_RETURNTYPE
  ldc.i4.1
  stloc returntype
  br @NEXT
@NOT_RETURNTYPE:
  ldloc arg
  ldstr "-Wno-override-module"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse @NOT_NOOVERRIDE
  ldc.i4.1
  stloc nooverride
  br @NEXT
@NOT_NOOVERRIDE:
  // Any other option is accepted; anything else is a source file.
  ldloc arg
  ldstr "-"
  callvirt instance bool [mscorlib]System.String::StartsWith(string)
  brtrue @NEXT
  ldloc arg
  stloc source
  ldloc sources
  ldc.i4.1
  add
  stloc sources
@NEXT:
  ldloc i
  ldc.i4.1
  add
  stloc i
  br @LOOP
@NO_VALUE:
  ldstr "an option at the end of the command line needs a value"
  stloc problem
  br @FAIL

@DONE:
  ldloc output
  brtrue @HAS_OUTPUT
  ldstr "no '-o <exe>'"
  stloc problem
  br @FAIL
@HAS_OUTPUT:
  ldloc sources
  ldc.i4.1
  beq @ONE_SOURCE
  ldstr "not exactly one source file"
  stloc problem
  br @FAIL
@ONE_SOURCE:
  ldloc target
  brtrue @HAS_TARGET
  ldstr "no --target=<triple>"
  stloc problem
  br @FAIL
@HAS_TARGET:
  ldloc target
  ldstr "-msil"
  callvirt instance bool [mscorlib]System.String::EndsWith(string)
  brfalse @NATIVE_TARGET
  ldstr "--target is an MSIL triple, not a native one"
  stloc problem
  br @FAIL
@NATIVE_TARGET:
  ldloc source
  ldstr ".ll"
  callvirt instance bool [mscorlib]System.String::EndsWith(string)
  brfalse @NOT_IR
  ldloc nooverride
  brtrue @COPY
  ldstr "an IR source without -Wno-override-module"
  stloc problem
  br @FAIL
@NOT_IR:
  ldloc source
  ldstr ".c"
  callvirt instance bool [mscorlib]System.String::EndsWith(string)
  brtrue @C_SOURCE
  ldstr "the source is neither .c nor .ll"
  stloc problem
  br @FAIL
@C_SOURCE:
  ldloc freestanding
  brtrue @HAS_FREESTANDING
  ldstr "a C source without -ffreestanding (%{msil_cflags})"
  stloc problem
  br @FAIL
@HAS_FREESTANDING:
  ldloc returntype
  brtrue @HAS_RETURNTYPE
  ldstr "a C source without -Werror=return-type (%{msil_cflags})"
  stloc problem
  br @FAIL
@HAS_RETURNTYPE:
  ldloc include
  brtrue @COPY
  ldstr "a C source without -I <a directory with msil_test.h> (%{msil_cflags})"
  stloc problem
  br @FAIL

@COPY:
  call class [mscorlib]System.Reflection.Assembly [mscorlib]System.Reflection.Assembly::GetEntryAssembly()
  callvirt instance string [mscorlib]System.Reflection.Assembly::get_Location()
  ldloc output
  ldc.i4.1
  call void [mscorlib]System.IO.File::Copy(string, string, bool)
  ldc.i4.0
  ret

@FAIL:
  call class [mscorlib]System.IO.TextWriter [mscorlib]System.Console::get_Error()
  ldstr "fake-native: error: "
  ldloc problem
  ldstr " in: "
  call string [mscorlib]System.String::Concat(string, string, string)
  ldstr " "
  ldarg.0
  call string [mscorlib]System.String::Join(string, string[])
  call string [mscorlib]System.String::Concat(string, string)
  callvirt instance void [mscorlib]System.IO.TextWriter::WriteLine(string)
  ldc.i4.1
  ret
} // end of method '<fake>compile'
