// Data import through a startup accessor, the Framework-compatible way:
// kernel32!LoadLibraryW + GetProcAddress give the address of msvcrt.dll's
// data export '_iob' (the FILE array MinGW's msvcrt headers index for
// stdout), and the program writes through &_iob[1] with msvcrt's fputs.
// The address is computed once in <Module>::.cctor and kept in an address
// field, the same shape as Phase 1's per-global fields (D12).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'data-win' { .ver 0:0:0:0 }

.method public hidebysig static pinvokeimpl("kernel32" cdecl) native int 'LoadLibraryW'(native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("kernel32" cdecl) native int 'GetProcAddress'(native int, native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("msvcrt" cdecl) int32 'fputs'(native int, native int) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("msvcrt" cdecl) int32 'fflush'(native int) cil managed preservesig {}

.field public static initonly native int '_iob'

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldstr "msvcrt.dll"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalUni(string)
  call native int 'LoadLibraryW'(native int)
  ldstr "_iob"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call native int 'GetProcAddress'(native int, native int)
  dup
  brtrue.s ok
  ldstr "data import '_iob' not found in msvcrt.dll"
  newobj instance void [mscorlib]System.EntryPointNotFoundException::.ctor(string)
  throw
ok:
  stsfld native int '_iob'
  ret
}

.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 3
  ldstr "hello through msvcrt's _iob[1] (data import)\n"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  ldsfld native int '_iob'
  ldc.i4.s 48      // sizeof(FILE) in msvcrt.dll on x64
  add
  call int32 'fputs'(native int, native int)
  pop
  ldsfld native int '_iob'
  ldc.i4.s 48
  add
  call int32 'fflush'(native int)
  pop
  ldc.i4.0
  ret
}
