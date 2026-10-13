// e13: what happens when int64 and native int are mixed without conv.i / conv.i8
// (the design keeps pointers as i64 in MachineInstrs). Each case is its own method so a JIT
// rejection (InvalidProgramException) is caught by the caller.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e13_i64ptr {}
.module e13_i64ptr.dll

.method assembly static int32 'sq'(int32 x) cil managed { .maxstack 2 ldarg.0 ldarg.0 mul ret }
.method assembly static native int 'take_ni'(native int p) cil managed { .maxstack 1 ldarg.0 ret }
.method assembly static int64 'take_i8'(int64 p) cil managed { .maxstack 1 ldarg.0 ret }

// 1. int64 used directly as an address for ldind/stind
.method assembly static int32 'c1'(native int buf) cil managed
{ .maxstack 2  .locals init (int64 a)  ldarg.0 conv.i8 stloc.0  ldloc.0 ldc.i4 41 stind.i4  ldloc.0 ldind.i4  ldc.i4.1 add  ret }
// 2. native int + int64 (binary op on mixed types)
.method assembly static int32 'c2'(native int buf) cil managed
{ .maxstack 2  ldarg.0 ldc.i8 4 add  ldc.i4 42 stind.i4  ldarg.0 ldc.i4.4 add ldind.i4  ret }
// 3. stloc int64 value into a native int local, and native int into an int64 local
.method assembly static int32 'c3'(native int buf) cil managed
{ .maxstack 2  .locals init (native int p, int64 q)  ldarg.0 conv.i8 stloc.0  ldloc.0 stloc.1  ldloc.1 ldc.i4 43 stind.i4  ldarg.0 ldind.i4  ret }
// 4. pass int64 to a native int parameter and native int to an int64 parameter
.method assembly static int32 'c4'(native int buf) cil managed
{ .maxstack 2  ldarg.0 conv.i8 call native int 'take_ni'(native int)  call int64 'take_i8'(int64)  ldc.i4 44 stind.i4  ldarg.0 ldind.i4  ret }
// 5. calli through an int64 function pointer
.method assembly static int32 'c5'(native int buf) cil managed
{ .maxstack 2  ldc.i4 7  ldftn int32 'sq'(int32) conv.i8  calli int32(int32)  ldc.i4 -4 add  ret }
// 6. return int64 from a method declared to return native int
.method assembly static native int 'c6_inner'(int64 x) cil managed { .maxstack 1 ldarg.0 ret }
.method assembly static int32 'c6'(native int buf) cil managed
{ .maxstack 1  ldc.i8 46 call native int 'c6_inner'(int64) conv.i4 ret }
// 7. compare/branch int64 against native int
.method assembly static int32 'c7'(native int buf) cil managed
{ .maxstack 2  ldarg.0 ldarg.0 conv.i8 beq.s EQ  ldc.i4.0 ret  EQ: ldc.i4 47 ret }

.method assembly static void 'run'(native int fp, native int buf, string name) cil managed
{
  .maxstack 3
  .try
  {
    ldstr "  "  ldarg.2  call string [mscorlib]System.String::Concat(string, string)  call void [mscorlib]System.Console::Write(string)
    ldarg.1  ldarg.0  calli int32(native int)
    ldstr " -> "  call void [mscorlib]System.Console::Write(string)
    call void [mscorlib]System.Console::WriteLine(int32)
    leave.s DONE
  }
  catch [mscorlib]System.Exception
  {
    callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
    ldstr " -> "  call void [mscorlib]System.Console::Write(string)
    call void [mscorlib]System.Console::WriteLine(object)
    leave.s DONE
  }
DONE:
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (native int buf)
  ldc.i4 64  conv.i  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)  stloc.0
  ldftn int32 'c1'(native int)  ldloc.0  ldstr "1 int64 as ldind/stind address"        call void 'run'(native int, native int, string)
  ldftn int32 'c2'(native int)  ldloc.0  ldstr "2 native int + int64"                  call void 'run'(native int, native int, string)
  ldftn int32 'c3'(native int)  ldloc.0  ldstr "3 stloc int64<->native int"            call void 'run'(native int, native int, string)
  ldftn int32 'c4'(native int)  ldloc.0  ldstr "4 int64 arg to native int param"       call void 'run'(native int, native int, string)
  ldftn int32 'c5'(native int)  ldloc.0  ldstr "5 calli through int64"                 call void 'run'(native int, native int, string)
  ldftn int32 'c6'(native int)  ldloc.0  ldstr "6 ret int64 as native int"             call void 'run'(native int, native int, string)
  ldftn int32 'c7'(native int)  ldloc.0  ldstr "7 beq int64 vs native int"             call void 'run'(native int, native int, string)
  ldc.i4.0
  ret
}
