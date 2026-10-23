// A program with a managed and a native dependency (../../dep-local.test,
// dep-wsl.test; recon p2/tests-p2 2.5, E5): main returns
// mylib's 'Contoso.Lib'::add(add3(10, 20, 5), 7) = 42, where add3 is a
// P/Invoke into the test-built native library 'native' (native.dll on
// Windows, libnative.so on Linux; both runtimes probe the program's
// directory). Without mylib.dll beside it the program stops with a
// FileNotFoundException, without the native library with a
// DllNotFoundException.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'mylib' { .ver 0:0:0:0 }
.assembly 'prog' { .ver 0:0:0:0 }

.method assembly static pinvokeimpl("native" cdecl) int32 'add3'(int32, int32, int32) cil managed preservesig
{
}

.method public static int32 'main'() cil managed
{
  .maxstack 3
  ldc.i4.s 10
  ldc.i4.s 20
  ldc.i4.5
  call int32 'add3'(int32, int32, int32)
  ldc.i4.7
  call int32 [mylib]'Contoso.Lib'::'add'(int32, int32)
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
