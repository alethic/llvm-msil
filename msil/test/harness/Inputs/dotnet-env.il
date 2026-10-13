// Reads the environment the harness passes with --env (msil_dotnet_env):
// exit status = len(MSIL_TEST_ENV) + 16 * len(MSIL_TEST_ENV2), where an unset
// variable counts as length 0.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'dotnet-env' { .ver 0:0:0:0 }

.method assembly static int32 'env_length'(string) cil managed
{
  .maxstack 2
  ldarg.0
  call string [mscorlib]System.Environment::GetEnvironmentVariable(string)
  dup
  brtrue @BB0_2
  pop
  ldc.i4.0
  ret
@BB0_2:
  callvirt instance int32 [mscorlib]System.String::get_Length()
  ret
} // end of method 'env_length'

.method public static int32 'main'() cil managed
{
  .maxstack 3
  ldstr "MSIL_TEST_ENV"
  call int32 'env_length'(string)
  ldstr "MSIL_TEST_ENV2"
  call int32 'env_length'(string)
  ldc.i4.s 16
  mul
  add
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
