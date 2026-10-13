// A module initializer in the shape of plan D12: <Module>::.cctor allocates
// the data segment with Marshal::AllocHGlobal, zeroes it, sets the address
// field of the global 'counter' and then runs the C constructors ('call void
// ctor1()'). The constructor prints a line through the windows-msvc 'puts'
// import (plan D14: ucrtbase) and stores 40 in 'counter'; the ordinary
// method 'main' returns counter + 2. msil-jitall.py runs the initializer
// too: force-JITting any method of <Module> runs <Module>::.cctor first, so
// the import must resolve wherever jitall runs.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'cctor-puts-windows' { .ver 0:0:0:0 }

.method assembly static void 'ctor1'() cil managed
{
  .maxstack 2
  ldstr "module initializer ran"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call int32 'puts'(native int)
  pop
  ldsfld native int 'counter'
  ldc.i4.s 40
  stind.i4
  ret
} // end of method 'ctor1'

.method public static int32 'main'() cil managed
{
  .maxstack 2
  ldsfld native int 'counter'
  ldind.i4
  ldc.i4.2
  add
  ret
} // end of method 'main'

// ---- undefined external functions: P/Invoke ----
.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}

// ---- data segment: 4 bytes (0 initialized + 4 zero), align 4 ----
.field private static initonly native int '<msil>data'
.field public static initonly native int 'counter'      // +0

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldc.i4.4
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int '<msil>data'
  ldsfld native int '<msil>data'
  ldc.i4.0
  ldc.i4.4
  initblk
  ldsfld native int '<msil>data'
  stsfld native int 'counter'
  call void 'ctor1'()
  ret
} // end of method '.cctor'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
