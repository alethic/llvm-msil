// A module initializer that needs the environment variable MSIL_TEST_ENV: it
// calls String::get_Length on
// Environment::GetEnvironmentVariable("MSIL_TEST_ENV"), which throws a
// NullReferenceException when the variable is unset. Force-JITting any
// method of <Module> runs the initializer first, so msil-jitall.py reports
// every method as failed unless the variable reaches jitall (--env, or lit's
// msil_dotnet_env).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'jitall-env-cctor' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.0
  ret
} // end of method 'main'

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 1
  ldstr "MSIL_TEST_ENV"
  call string [mscorlib]System.Environment::GetEnvironmentVariable(string)
  callvirt instance int32 [mscorlib]System.String::get_Length()
  pop
  ret
} // end of method '.cctor'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
