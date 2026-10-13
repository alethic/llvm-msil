// A stand-in for a gtest binary in gtest.test, which tests the gate script
// msil-gtest.ps1 (its -Exe must be an executable). Assembled with
// 'ilasm -exe', it runs directly on the .NET Framework, like a native
// executable. It ignores its arguments, prints the file named by the
// environment variable FAKE_PROGRAM_OUTPUT and exits with the status in
// FAKE_PROGRAM_STATUS.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'fake-program' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldstr "FAKE_PROGRAM_OUTPUT"
  call string [mscorlib]System.Environment::GetEnvironmentVariable(string)
  call string [mscorlib]System.IO.File::ReadAllText(string)
  call void [mscorlib]System.Console::Write(string)
  ldstr "FAKE_PROGRAM_STATUS"
  call string [mscorlib]System.Environment::GetEnvironmentVariable(string)
  call int32 [mscorlib]System.Int32::Parse(string)
  ret
} // end of method '<msil>Main'
