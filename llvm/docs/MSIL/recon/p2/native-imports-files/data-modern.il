// Data imports through a startup accessor on modern .NET: NativeLibrary.Load
// + GetExport give the address of a native data export; <Module>::.cctor
// stores it in an address field (Phase 1's per-global field shape, D12), and
// code reads the variable through it exactly as it reads a global defined in
// the module. Build twice: -DLINUX (glibc stdout/environ/optind) or Windows
// (msvcrt _iob). Runs on .NET 8 and 10 only (NativeLibrary is .NET Core 3.0+).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly 'data-modern' { .ver 0:0:0:0 }

#ifdef LINUX
.method public hidebysig static pinvokeimpl("libc.so.6" cdecl) int32 'fputs'(native int, native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("libc.so.6" cdecl) int32 'fflush'(native int) cil managed preservesig {}
.field public static initonly native int 'stdout'
.field public static initonly native int 'environ'
.field public static initonly native int 'optind'
#else
.method public hidebysig static pinvokeimpl("msvcrt" cdecl) int32 'fputs'(native int, native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("msvcrt" cdecl) int32 'fflush'(native int) cil managed preservesig {}
.field public static initonly native int '_iob'
#endif

.method private hidebysig static native int '<msil>dataimport'(string lib, string name) cil managed
{
  .maxstack 2
  ldarg.0
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeLibrary::Load(string)
  ldarg.1
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeLibrary::GetExport(native int, string)
  ret
}

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 2
#ifdef LINUX
  ldstr "libc.so.6"
  ldstr "stdout"
  call native int '<msil>dataimport'(string, string)
  stsfld native int 'stdout'
  ldstr "libc.so.6"
  ldstr "environ"
  call native int '<msil>dataimport'(string, string)
  stsfld native int 'environ'
  ldstr "libc.so.6"
  ldstr "optind"
  call native int '<msil>dataimport'(string, string)
  stsfld native int 'optind'
#else
  ldstr "msvcrt.dll"
  ldstr "_iob"
  call native int '<msil>dataimport'(string, string)
  stsfld native int '_iob'
#endif
  ret
}

.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (native int f)
#ifdef LINUX
  ldsfld native int 'stdout'
  ldind.i                      // FILE *stdout
  stloc.0
  ldstr "optind = "
  ldsfld native int 'optind'
  ldind.i4
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Concat(object, object)
  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "environ[0] starts with "
  ldsfld native int 'environ'
  ldind.i                      // char **environ
  ldind.i                      // char *environ[0]
  call string [mscorlib]System.Runtime.InteropServices.Marshal::PtrToStringAnsi(native int)
  ldc.i4.0
  ldc.i4.4
  callvirt instance string [mscorlib]System.String::Substring(int32, int32)
  call string [mscorlib]System.String::Concat(string, string)
  call void [mscorlib]System.Console::WriteLine(string)
#else
  ldsfld native int '_iob'
  ldc.i4.s 48
  add
  stloc.0
#endif
  ldstr "hello through a native data import of stdout\n"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  ldloc.0
  call int32 'fputs'(native int, native int)
  pop
  ldloc.0
  call int32 'fflush'(native int)
  pop
  ldc.i4.0
  ret
}
